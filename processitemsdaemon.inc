<?php

/**
 * @file
 * Contains the Daemon class for handling items in a process. This class is
 * useful if you have a list of items you are processing in your process
 * callback. Otherwise, you probably should just extend the DrushDaemon class.
 */

class ProcessItemsDaemon extends DrushDaemon {
  /**
   * Optional callback function called when the process items array is empty, for
   * non-OOP drush command processes.
   *
   * @var string
   */
  protected $processItemsEmptyCallback;

  /**
   * Override the status method here to add some statistics about number of items.
   */
  protected function status() {
    parent::status();
    if ($count = count($this->getAllItems())) {
      drush_log(dt('There are !num process items remaining.', array('!num' => $count)), 'ok');
    }
  }

  public function getAllItems() {
    return $this->statusHandler->processItems;
  }

  public function getItems($count = 100) {
    $items = $this->getAllItems();

    if (count($items) <= $count) {
      return $items;
    }
    return array_slice($items, 0, $count, TRUE);
  }

  public function setItems($items = array()) {
    $this->statusHandler->processItems = $items;
    $this->statusHandler->writeStatusFile();
  }

  public function unSetItems($items = array()) {
    foreach (array_keys($items) as $key) {
      unset($this->statusHandler->processItems[$key]);
    }
    // If there are no items left, tell the implementor.
    if (empty($this->statusHandler->processItems)) {
      $this->processItemsEmpty();
    }
    $this->statusHandler->writeStatusFile();
  }

  /**
   * The process Items array is empty; let the implementor know. If you are
   * extending this class, you'll want to override this function completely.
   */
  protected function processItemsEmpty() {
    if (isset($this->processItemsEmptyCallback)) {
      $callback = $this->processItemsEmptyCallback;
      if (function_exists($callback)) {
        return $callback();
      }
      else {
        drush_log(dt('The processItemsEmptyCallback !callback() does not exist.', array('!callback' => $callback)), 'warning');
      }
    }
  }
}