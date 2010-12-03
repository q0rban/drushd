<?php
// $Id$

/**
 * @file
 * 
 */

class DrushDaemonExample extends DrushDaemon {
  function __construct($command) {
    parent::__construct($command);
    $this->healthChecks[] = 'exampleHealthCheck';
  }

  protected function process() {
    // Don't forget to put the process in hibernation if nothing needs to be done!
    drush_print('well hello there');
    return TRUE;
  }

  protected function needsProcessing() {
    static $i = 0;
    return $i++ > 3;
  }

  protected function exampleHealthCheck() {
    static $i = 0;
    return $i++ < 100;
  }
}