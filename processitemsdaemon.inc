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
   * Process failure threshold. If the percantage of failed process items from a
   * batch of items sent to processItems reaches this number, the daemon will quit
   * processing and log an error message. Defaults to 100%, but you can change the
   * default with the drush option failure-threshold="0.05" or by overriding the
   * value in a class extension.
   *
   * @var float
   */
  protected $failureThreshold = 1.00;

  /**
   * Override the status method here to add some statistics about number of items.
   */
  protected function status() {
    parent::status();
    if ($count = count($this->getAllItems())) {
      drush_log(dt('There are !num process items remaining.', array('!num' => $count)), 'ok');
    }
  }


  /**
   * Implementation of process method, to iterate over process items.
   */
  protected function process() {
    // If this is not an OO daemon, process will be a procedural function called
    // by DrushDaemon::process(), so just return the parent::process() function.
    if (isset($this->processCallback)) {
      return parent::process();
    }

    return $this->processItems();
  }

  /**
   * Processes a batch of items, checking for failure rate of this batch.
   *
   * @param array $items
   *   An array of items to process. If left NULL, ProcessItemsDaemon::getItems()
   *   will be used to populate the array.
   */
  protected function processItems($items = NULL) {
    $items = is_null($items) ? $this->getItems() : $items;

    $failures = array();
    $count = count($items);
    // We only want to check failure rate if we actually have enough process
    // items to make it worth our while.
    $check_failure = $count * $this->failureThreshold > 1;

    foreach ($items as $key => $item) {
      if ($this->processItem($item)) {
        $this->total_successes++;
        $this->successes_since_feedback++;
      }
      else {
        $failures[] = $key;
      }
      $this->total_processed++;
      $this->processed_since_feedback++;
    }

    if ($check_failure && count($failures) / $count >= $this->failureThreshold) {
      // Log an error with the array keys and return FALSE.
      $dt_args = array(
        '!num' => count($failures),
        '@keys' => implode(',', $failures),
      );
      drush_log(dt('!num process items failed. Here are the array keys of the failed items: @keys', $dt_args), 'failure');
      return FALSE;
    }

    // If we're below the failure threshold, return TRUE and unset the items.
    $this->unSetItems($items);
    return TRUE;
  }

  /**
   * Where the actual item processing occurs. Must return TRUE for a successful
   * process operation.
   *
   * @see NodeAccessRebuildDaemon::processItem() for an example.
   * @param $item
   *   The item for processing.
   * @return
   *   Boolean TRUE if the item was successfully processed.
   */
  protected function processItem() {}

  /**
   * Returns the full array of all remaining process items.
   */
  public function getAllItems() {
    return $this->statusHandler->processItems;
  }

  /**
   * Returns the next batch of items for processing.
   */
  public function getItems($count = 100) {
    $items = $this->getAllItems();

    if (count($items) <= $count) {
      return $items;
    }
    return array_slice($items, 0, $count, TRUE);
  }

  /**
   * Callback for initial set up of the process items.
   */
  public function setItems($items = array()) {
    $this->statusHandler->processItems = $items;
    $this->statusHandler->writeStatusFile();
  }

  /**
   * Unset a batch of items.
   */
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

  /**
   * Detects Drush options, setting up instance variables where needed.
   */
  protected function detectOptions() {
    parent::detectOptions();

    if ($threshold = drush_get_option('failure-threshold')) {
      $this->failureThreshold = (float) $threshold;
    }
  }
}