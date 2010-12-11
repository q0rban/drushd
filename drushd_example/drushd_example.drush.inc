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
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_DATABASE,
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

  // This is a helper command to get a fresh node_access_needs_rebuild().
  // Since node_access_needs_rebuild is a variable, it gets statically cached,
  // but putting it in a drush command allows the daemon to retrieve a fresh
  // return value from the command, without having to refork each time.
  $items['node-access-needs-rebuild'] = array(
    'hidden' => TRUE,
    'description' => 'Return or set whether node access grants need to be rebuilt.',
    'arguments' => array(
      'value' => 'Optional value to set node access needs rebuild to, either 0 or 1.'
    ),
    'aliases' => array('nanr'),
  );

  return $items;
}

/**
 * Callback for drush node-access-needs-rebuild.
 */
function drush_drushd_example_node_access_needs_rebuild() {
  // If we are in the backend context, print the status directly.
  if (drush_get_context('DRUSH_BACKEND')) {
    drush_print((string) node_access_needs_rebuild());
    return;
  }

  if (($args = drush_get_arguments()) && count($args) > 1) {
    node_access_needs_rebuild((bool) $args[1]);
    drush_log(dt('Node access needs rebuild has been set to !val', array('!val' => $args[1])));
  }
  $message = 'Node access grants do not need to be rebuilt.';
  if (_drushd_example_node_access_needs_rebuild() == 1) {
    $message = 'Node access grants need to be rebuilt.';
  }
  drush_log(dt($message), 'ok');
}

/**
 * Retrieve the current bot status, clearing the variable cache first.
 */
function _drushd_example_node_access_needs_rebuild() {
  // We first clear the variables cache and then invoke the command to get a
  // fresh status. Otherwise the static variable may contain stale data.
  cache_clear_all('variables', 'cache');
  $ret = drush_backend_invoke('node-access-needs-rebuild', array(), 'GET', FALSE);
  return $ret['output'];
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

function drushd_example_simple_needs_processing() {
  return TRUE;
}

function drushd_example_health_check() {
  static $i = 0;

  return TRUE; //$i++ < 100;
}