<?php

/**
 * Implements of hook_drush_command().
 */
function my_module_drush_command() {
  $items = array();

  $items['mt-command'] = array(
    'description' => 'This command manages all multithread processes.',
    'arguments' => array(
      'limit' => 'Total number of jobs to up - use 0 for all.',
      'batch_size' => 'Number of jobs each thread will work on.',
      'threads' => 'Number of threads',
    ),
    'options' => array(
      'offset' => 'A starting offset should you want to start 1000 records in',
    ),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_ROOT,
  );

  $items['update-alias'] = array(
    'description' => 'Update the node url alias.',
    'arguments' => array(
      'name' => 'The name of this process, this will be the thread id',
      'limit' => 'Number of jobs each thread will work on.',
      'offset' => 'A starting offset should you want to start 1000 records in',
    ),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_ROOT,
  );

  return $items;
}

/**
 * This is a multi threaded management.
 * Each thread will be told to run the drush command:
 * drush update-alias
 *
 * @param int $limit
 *    The total number of jobs to process. Each job is an
 *    execution of drush update-alias. If $limit = 0, then it retrieves all
 *    nodes for the chosen content-types.
 *
 * @param int $batch_size
 *    Number of nodes updated per single job.
 *
 * @param int $thread
 *    Number of threads.
 */
function drush_mt_command($limit = 10, $batch_size = 10, $threads = 1) {
  $time = time();

  try {
    // Choose the content-type you will update.
    $node_types = array(
      'page',
      'article',
    );

    /*
    I set this global var because I need this value into
    _mt_command_setup() function.
    */
    $GLOBALS['node_types'] = implode(',', $node_types);

    $starting_offset = drush_get_option('offset', 0);

    if($limit == 0) {
      // Retrieve all records
      $query = db_select('node', 'n');
      $result = $query->fields('n', array('nid'))
        ->orderBy('n.nid', 'ASC')
        ->condition('type', $node_types, 'IN')
        ->execute();

      $limit = $result->rowCount();
    }

    drush_print("Going to work on {$limit} jobs with {$threads} threads...");

    drush_thread_manager($limit, $batch_size, $threads, '_mt_command_setup',
      '_mt_command_teardown', $starting_offset);
  }
  catch (Exception $e) {
    drush_set_error($e->getMessage());
  }

  $time = time() - $time;
  drush_print("Time taken with {$threads} threads: {$time} seconds");
}

/**
 * Updates url alias.
 */
function drush_update_alias($thread, $limit, $offset, $node_types = '') {

  $node_types_array = explode(',', $node_types);

  drush_print("Thread $thread is working for $limit nodes starting " .
    "from $offset...");

  // Retrieve the next group of nids.
  $query = db_select('node', 'n');

  $result = $query->fields('n', array('nid'))
      ->orderBy('n.nid', 'ASC')
      ->condition('type', $node_types_array, 'IN')
      ->range($offset, $limit)->execute();

  // Load pathauto.module from the pathauto module.
  module_load_include('module', 'pathauto', 'pathauto');

  $current_node = 0;
  foreach ($result as $row) {
    // Create the nids array
    $node = node_load($row->nid);
    pathauto_node_update_alias($node, 'update');

    // Update our progress information.
    $current_node = $row->nid;
  }
  drush_print("## Last node processed is nid=$current_node.");
}
