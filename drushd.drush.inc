<?php

function drushd_drush_init() {
  if (!($command = &drush_get_context('command'))) {
    return;
  }

  if (isset($command['daemon'])) {
    $class = isset($command['daemon']['class']) ? $command['daemon']['class'] : 'DrushDaemon';
    drushd_include_files($command);

    // Necessary evil for simple callbacks.
    global $daemon;
    $daemon = new $class($command);

    // If we're in the backend, assume this is the daemon process.
    if (drush_get_context('DRUSH_BACKEND')) {
      $daemon->backendInvoke();
    }
    // If this is not a forking command, or debugging is on, run the command directly.
    else if (!$daemon->commandRequiresFork() || drush_get_option('debug', FALSE)) {
      $daemon->callCommandMethod();
    }
    // Otherwise, summon the daemon!
    else {
      $daemon->summon();
    }
    $daemon->drushExit();
  }
}

/**
 * Just a dummy callback so Drush doesn't whine at us. If your drush command is
 * extending the DrushDaemon command and has it's own process method, you
 * probably want to set the callback-hook to 'drushd' in hook_drush_command, so
 * that you don't get whined at either! ;)
 *
 * @see drushd_example.drush.inc.
 */
function drush_drushd() {
  drush_log(dt('drush_drushd() was called. You probably want to create your own process method in your DrushDaemon extension.'), 'warning'); 
}

function _drushd_print_log($entry) {
  drush_print($entry['message']);
}

function drushd_include_files($command) {
  require_once 'drushdaemon.inc';
  require_once 'processitemsdaemon.inc';
  require_once 'outputhandler.inc';
  require_once 'statushandler.inc';

  if (isset($command['daemon']['file'])) {
    $path = isset($command['daemon']['file path']) ? $command['daemon']['file path'] : $command['path'];
    $path .= '/'. $command['daemon']['file'];
    require_once $path;
  }
}