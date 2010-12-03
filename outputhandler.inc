<?php
// $Id$

/**
 * @file
 * Output handler, to log all drush_print, drush_log, etc to a log file.
 */

class OutputHandler {
  private $filename;
  private $logFile = null;
  private $lastClose;

  public function __construct($filename) {
    $this->setFilename($filename);
  }

  public function __destruct() {
    if($this->logFile) {
      $this->end();
    }
  }

  public function setFilename($filename) {
    $this->filename = $filename;
  }

  public function getFilename() {
    return $this->filename;
  }

  public function start() {
    $this->logFile = @fopen($this->filename,'a+');
  
    if(!$this->logFile) {
      throw new Exception('Cannot open '. $this->filename. ' for writing!');
    }

    ob_start(array($this, 'outputWriter'));
  }

  public function end() {
    @ob_end_flush();
    if($this->logFile) {
      fclose($this->logFile);
    }

    $this->logFile = null;
  }

  public function outputWriter($buffer) {
    fwrite($this->logFile, $buffer);
  }
}