<?php
// $Id$

/**
 * @file
 * 
 */

class OutputHandler {
  private $_filename;
  private $_fp = null;

  public function __construct($filename) {
    $this->setFilename($filename);
  }

  public function __destruct() {
    if($this->_fp) {
      $this->end();
    }
  }

  public function setFilename($filename) {
    $this->_filename = $filename;
  }

  public function getFilename() {
    return $this->_filename;
  }

  public function start() {
    $this->_fp = @fopen($this->_filename,'w');
  
    if(!$this->_fp) {
      throw new Exception('Cannot open '.$this->_filename.' for writing!');
    }

    ob_start(array($this,'outputWriter'));
  }

  public function end() {
    @ob_end_flush();
    if($this->_fp)
      fclose($this->_fp);
    
    $this->_fp = null;
  }

  public function outputWriter($buffer) {
    fwrite($this->_fp,$buffer);
  }
}