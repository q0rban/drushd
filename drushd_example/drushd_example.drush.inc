<?php


/**
 * Implementation of hook_drush_command().
 */
function drushd_example_drush_command() {
  $items = array();

  $items['drushd-example'] = array(
    'daemon' => array(
      'class' => 'DrushDaemonExample',
      'file' => 'drushd_example.inc',
      // file path is only necessary if your include is in a separate directory
      // than this file.
      //'file path' => dirname(__FILE__),
    ),

    // Since we have our own custom process method, you'll need to specify this
    // command-hook here, or Drush will try to find a callback function for this
    // command and whine at you about it.
    'command-hook' => 'drushd',
  );

  $items['drushd-example-simple'] = array(
    'daemon' => array(
      'process callback' => 'drushd_example_process',
      'options' => array(
        'healthChecks' => array('checkMemoryUsage', 'drushd_example_health_check'),
      ),
    ),
    'command-hook' => 'drushd',
  );

  return $items;
}

/**
 * Process function for drushd-example-simple 
 */
function drushd_example_process() {
  drush_print('hi');
  return TRUE;
}

function drushd_example_health_check() {
  static $i = 0;

  return $i++ < 100;
}