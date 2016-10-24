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

    drush_log("Going to work on {$limit} jobs with {$threads} threads...");

    drush_thread_manager($limit, $batch_size, $threads, '_mt_command_setup',
      '_mt_command_teardown', $starting_offset);
  }
  catch (Exception $e) {
    drush_set_error($e->getMessage());
  }

  $time = time() - $time;
  drush_log("Time taken with {$threads} threads: {$time} seconds");
}

/**
 * Updates url alias.
 */
function drush_update_alias($thread, $limit, $offset, $node_types = '') {

  $node_types_array = explode(',', $node_types);

  drush_log("Thread $thread is working for $limit nodes starting " .
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
  drush_log("## Last node processed is nid=$current_node.");
}

/**
 * Create the drush command to execute the alias update.
 *
 * @param int $thread_id
 *   A thread identifier which is managing this update.
 * @param int $limit
 *   Total number of items to update.
 * @param int $offset
 *   Offset to start the update at.
 *
 * @return string
 *   A command which can be executed on the command line.
 */
function _mt_command_setup($thread_id, $limit, $offset) {
  $node_types = $GLOBALS['node_types'];
  $cmd = "drush update-alias $thread_id $limit $offset $node_types";
  return $cmd;
}

/**
 * A test multi threaded teardown function.
 *
 * @param int $thread_id
 *   The thread which just completed.
 */
function _mt_command_teardown($thread_id) {
  // Nothing to do in this example.
}

/**
 * A multithreading manager for drush.
 *
 * @param int $job_count
 *   The total number of jobs to process
 * @param int $job_batch_size
 *   The number of jobs handed to each thread
 * @param int $thread_count
 *   The maximum number of active threads
 * @param string $setup_function
 *   The function to generate the thread command. This is written for your
 *   application its signature is:
 *   my_setup_function($thread_id, $limit, $offset) : string;
 *   Where $thread_id is the unique thread identifier, $limit is the total
 *   number of records to process and offset is the starting point. The
 *   function should return a string which is a shell command that the thread
 *   will execute in a separate process.
 * @param string $teardown_function
 *   A function which runs when a thread completes. It's signature is:
 *   my_teardown_function($thread_id);
 *   Your business logic can then run any cleanup which occurs when the
 *   thread completes.
 * @param int $starting_offset
 *   (optional) Start x records into the process.
 */
function drush_thread_manager($job_count, $job_batch_size, $thread_count, $setup_function, $teardown_function = '', $starting_offset = 0) {

  if (!function_exists('proc_open')) {
    throw new Exception('proc_open command is needed to use multi threading.');
  }

  $jobs_remaining = $job_count;
  $processes = array();
  $running = TRUE;
  $offset = $starting_offset;
  $start = time();

  $descriptor_spec = array(
    0 => array("pipe", "r"),
    1 => array("pipe", "w"),
    2 => array("pipe", "w"),
  );

  while ($running) {

    // Fill all empty threads.
    for ($thread_id = 0; $thread_id < $thread_count; $thread_id++) {
      if ($jobs_remaining > 0 && !isset($processes[$thread_id])) {
        $pipes = array();
        $cmd = $setup_function($thread_id, $job_batch_size, $offset);
        $process = proc_open($cmd, $descriptor_spec, $pipes);

        drush_log("Thread {$thread_id} starts at offset $offset. Jobs remaining: $jobs_remaining");

        foreach ($pipes as $pipe) {
          stream_set_blocking($pipe, FALSE);
        }

        $offset += $job_batch_size;
        $jobs_remaining -= $job_batch_size;

        $processes[$thread_id] = array(
          'process' => $process,
          'pipes' => $pipes,
        );

        //sleep(1);
      }
    }

    // Monitor running threads.
    for ($thread_id = 0; $thread_id < $thread_count; $thread_id++) {
      if (!isset($processes[$thread_id])) {
        continue;
      }

      $process_running = _mt_monitor_process($thread_id, $processes[$thread_id]['process'], $processes[$thread_id]['pipes']);

      if (!$process_running) {
        unset($processes[$thread_id]);

        if (!empty($teardown_function)) {
          $teardown_function($thread_id);
        }

        $speed = ($job_count - $jobs_remaining) / (time() - $start);
        $end_date = date('G:i:s d/m/y', ($job_count / $speed) + $start);
        $speed = floor($speed * 60);

        drush_log("Thread {$thread_id} finished. Average speed is {$speed} jobs/minute. Estimated completion at {$end_date}");
      }
    }

    // Check for endgame.
    if ($jobs_remaining < 1 && empty($processes)) {
      $running = FALSE;
    }
    else {
      // Pause the master process.
      sleep(1);
    }
  }
}

/**
 * Monitor a process.
 *
 * This function checks a running process.
 * If it has finished it is closed down safely.
 *
 * @return bool
 *   TRUE if the process is still running
 *   FALSE if it is not running.
 */
function _mt_monitor_process($thread_id, $process, $pipes) {
  $status = proc_get_status($process);

  foreach ($pipes as $id => $pipe) {
    if ($id == 0) {
      // Don't read from stdin!
      continue;
    }

    $messages = stream_get_contents($pipe);

    if (!empty($messages)) {
      foreach (explode("\n", $messages) as $message) {
        $message = str_replace('[error]', '', $message);
        $message = str_replace('[completed]', '', $message);
        $message = trim($message);
        if (!empty($message)) {
          if ($id == 1) {
            // Message from stdout.
            drush_log(" -> T$thread_id: $message");
          }
          else {
            // Message from stderr.
            drush_set_error(" -> T$thread_id: $message");
          }
        }
      }
    }
  }

  if (!$status['running']) {
    foreach ($pipes as $pipe) {
      fclose($pipe);
    }

    proc_close($process);
  }

  return $status['running'];
}
