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
    //sleep(1);
    drush_log('well hello there', 'ok');

    // Here you'd put some sort of check to see if this process succeeded.
    if (TRUE) {
      $this->successes_since_feedback++;
      $this->total_successes++;
    }
    $this->total_processed++;
    $this->processed_since_feedback++;

    // Only return FALSE if something happens that should stop all the processes.
    return TRUE;
  }

  protected function needsProcessing() {
    return TRUE;
  }

  protected function exampleHealthCheck() {
    static $i = 0;
    return $i++ < 1200;
  }
}