<?php

/**
 * @file
 * Example class to rebuild node access permissions on the fly.
 */

class NodeAccessRebuildDaemon extends ProcessItemsDaemon {
  /**
   * The actual process.
   */
  protected function process() {
    // This loads up 100 nodes to process.
    $nids = $this->getItems();

    // Delete the old records in one foul swoop.
    db_query("DELETE FROM {node_access} WHERE nid IN (%s)", implode(',', $nids));

    foreach ($nids as $nid) {
      // To preserve database integrity, only aquire grants if the node
      // loads successfully.
      if ($node = node_load($nid, NULL, TRUE)) {
        //drush_print($nid);
        node_access_acquire_grants($node);
        $this->total_successes++;
        $this->successes_since_feedback++;
      }
      $this->total_processed++;
      $this->processed_since_feedback++;
    }

    $this->unSetItems($nids);

    // Only return FALSE if something happens that should stop the daemon.
    return TRUE;
  }

  /**
   * Check to see if node access grants need to be rebuilt.
   */
  protected function needsProcessing() {
    // If we're hibernating, we need to use backend invoke to get the
    // node_access_needs_rebuild status, so as to get a fresh value from the
    // database. Otherwise, we'd just be retrieving what exists in the static
    // cache, which wouldn't do us much good. :)
    if ($this->getStatus() == StatusHandler::HIBERNATING) {
      $needs_rebuild = _drushd_example_node_access_needs_rebuild() == 1;
      // Now store the fresh copy in node_access_needs_rebuild, for performance.
      node_access_needs_rebuild($needs_rebuild);
    }

    $needs_processing = count(module_implements('node_grants')) && node_access_needs_rebuild();

    if ($needs_processing) {
      // If we don't have any queued nodes, fill up the processItems array.
      $count = count($this->getAllItems());
      if ($count === 0) {
        drush_log(dt('Selected all nodes from the database.'));
        $sql_result = db_query("SELECT nid FROM {node}");
        while ($nid = db_result($sql_result)) {
          $nids[$nid] = $nid;
        }

        if ($count = count($nids)) {
          $this->setItems($nids);
        }
        // If we don't have any nids, no reason to process.
        else {
          node_access_needs_rebuild(FALSE);
          $needs_processing = FALSE;
        }
      }
      drush_log(dt('There are !num nodes to rebuild.', array('!num' => $count)));
    }
    // Clean out the items if we don't need processing.
    else {
      $this->setItems();
    }

    return $needs_processing;
  }

  /**
   * There are no nodes left to rebuild, so tell node access.
   */
  protected function processItemsEmpty() {
    $this->progressMessage(DrushDaemon::RESULT_HIBERNATING);
    node_access_needs_rebuild(FALSE);
    drush_backend_invoke('cache-clear all');
  }
}