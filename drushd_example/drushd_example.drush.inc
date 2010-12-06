<?php


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
    'description' => "Daemon to rebuild node access permissions.",
    'drupal dependencies' => array('node'),
    'aliases' => array('nar'),
    'options' => array(
      '--feedback' => 'Frequency of progress messages, in seconds or items processed.',
    ),
    'examples' => array(
      'nar start --feedback="100 items"' => 'Rebuild node access permissions, logging a status message every 100 nodes.',
      'nar start --feedback="60 seconds"' => 'Rebuild node access permissions, logging a status message every 60 seconds.',
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