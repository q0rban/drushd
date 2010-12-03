<?php
// $Id$

/**
 * @file
 * 
 */

class DrushDaemonTest extends DrushDaemon {
  protected function process() {
    drush_print('well hello there');
  }
}