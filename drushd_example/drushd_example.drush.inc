<?php

/**
 * Implementation of hook_drush_help().
 */
function drushd_example_drush_help($section) {
  switch ($section) {
    case 'drush:node-access-rebuild':
      return dt('Daemon to rebuild node access permissions.');
    case 'drush:drushd-example-simple':
      return dt('');
  }
}

/**
 * Implementation of hook_drush_command().
 */
function drushd_example_drush_command() {
  $items = array();

  $items['node-access-rebuild'] = array(
    'daemon' => array(
      'class' => 'NodeAccessRebuildDaemon',
      'file' => 'nodeaccessrebuilddaemon.inc',
      // file path is only necessary if your include is in a separate directory
      // than this file.
      //'file path' => dirname(__FILE__),
    ),

    // Since we have our own custom process method, you'll need to specify this
    // command-hook here, or Drush will try to find a callback function for this
    // command and whine at you about it.
    'command-hook' => 'drushd',

    // Everything below here is just typical drush command parameters.
    'description' => 'Daemon to rebuild node access permissions.',
    'drupal dependencies' => array('node'),
    'aliases' => array('nar'),
    'options' => array(
      '--feedback' => 'Frequency of progress messages, in seconds or items processed.',
    ),
    // Good to include these arguments, so people know how to use the daemon.
    'arguments' => array(
      'start'    => 'Start the daemon.',
      'status'   => 'Display status information about the daemon.',
      'stop'     => 'Stop the daemon.',
      'restart'  => 'Restart the daemon',
      'show-log' => 'Show the log file.',
    ),
    'examples' => array(
      'drush nar start'                         => 'Start the daemon.',
      'drush nar start --feedback="100 items"'  => 'Log a status message every 100 nodes.',
      'drush nar start --feedback="60 seconds"' => 'Log a status message every 60 seconds.',
      'drush nar start --verbose'               => 'Log verbosely.',
      ' '   => '',
      'drush nar status' => 'Show the current status of the daemon.',
      '  '  => '',
      'drush nar stop'              => 'Stop the daemon, allowing 45 seconds for processing.',
      'drush nar stop --timeout=10' => 'Allow 10 seconds for processing.',
      'drush nar stop --queue'      => 'Queue the node access rebuild daemon to stop.',
      '   ' => '',
      'drush nar restart'              => 'Restart the daemon, allowing 45 seconds for processing.',
      'drush nar restart --timeout=10' => 'Allow 10 seconds for processing.',
      'drush nar restart --queue'      => 'Queue the node access rebuild daemon to restart.',
      '    ' => '',
      'drush nar show-log' => 'Show the log file, using less.',
      'drush nar show-log --watch' => 'Watch the log file.',
      'drush nar show-log --tail' => 'Show just the tail of the log file, to see recent messages.',
    ),
  );

  $items['drushd-example-simple'] = array(
    'daemon' => array(
      'options' => array(
        'processCallback' => 'drushd_example_simple_process',
        'healthChecks' => array('checkMemoryUsage', 'drushd_example_health_check'),
        'needsProcessingCallback' => 'drushd_example_simple_needs_processing',
      ),
    ),
    'command-hook' => 'drushd',
  );

  return $items;
}

function drushd_example_simple_needs_processing() {
  return TRUE;
}

/**
 * Process function for drushd-example-simple 
 */
function drushd_example_simple_process() {
  drush_print('This is pretty simple.');
  global $daemon;

  // Here you'd put some sort of check to see if this process succeeded.
  if (TRUE) {
    $daemon->successes_since_feedback++;
    $daemon->total_successes++;
  }
  $daemon->total_processed++;
  $daemon->processed_since_feedback++;

  // Only return FALSE if something happens that should stop all the processes.
  return TRUE;
}

function drushd_example_health_check() {
  static $i = 0;

  return TRUE; //$i++ < 100;
}