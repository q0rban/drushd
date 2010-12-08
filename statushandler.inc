<?php

class StatusHandler {
  /**
   * Status constants.
   *
   * @var int
   */
  const STOPPED     = 'stopped';
  const STARTED     = 'started';
  const HIBERNATING = 'hibernating';
  const REFORKING   = 'reforking';
  const STOPPING    = 'stopping';

  /**
   * The daemon object.
   *
   * @var int
   */
  protected $daemon;

  /**
   * The timestamp of when the status was last written to file.
   *
   * @var int
   */
  public $lastWritten;
  public function getLastWritten() { return $this->lastWritten; }

  /**
   * The process ID.
   *
   * @var int
   */
  public $pid;
  public function getPid() { return $this->pid; }

  /**
   * An array of items that need processing.
   *
   * @var array
   */
  public $processItems;

  /**
   * The command that was reforked.
   *
   * @var string
   */
  public $reforkedCommand;
  public function getReforkedCommand() { return $this->reforkedCommand; }

  /**
   * An array of timestamps of when a command was reforked, keyed by command.
   *
   * @var array
   */
  public $reforks;

  /**
   * The current status.
   *
   * @var string
   */
  public $status;

  /**
   * The path to the status file, containing current status and process id.
   *
   * @var string
   */
  protected $statusFilename = '';

  /**
   * The timestamp when the status file was last read.
   *
   * @var int
   */
  protected $statusLastRead = 0;

  /**
   * Duration in seconds that the status should reside in the static cache. When
   * this expires, the status is re-read from the status file.
   *
   * @var int
   */
  protected $statusTTL;

  /**
   * Constructor.
   *
   * @param DrushDaemon $daemon
   *   The DrushDaemon object.
   * @param string $filename
   *   The filename for the status file.
   */
  public function __construct(DrushDaemon $daemon, $filename) {
    $this->daemon = $daemon;
    $this->statusTTL = $daemon->getStatusTTL();
    $this->statusFilename = $filename;
    $this->detectStatus();
    if ($this->daemon->isDaemon() && function_exists('posix_getpid')) {
      $this->pid = posix_getpid();
    }
  }

  /**
   * Detect the current status of this process.
   */
  protected function detectStatus() {
    if ($this->readStatusFile()) {
      drush_log(dt('The status file has been read.'));
    }
    else {
      $this->status = StatusHandler::STOPPED;
    }
  }

  /**
   * Retrieve the current status of the daemon process.
   */
  public function getStatus($refresh = FALSE) {
    if ($refresh || $this->statusNeedsRefresh()) {
      $this->detectStatus();
    }
    return $this->status;
  }

  /**
   * Determine if the status file needs to be re-read.
   */
  public function statusNeedsRefresh() {
    $now = time();
    return ($now - $this->statusLastRead) >= $this->statusTTL;
  }

  /**
   * Set the current status of the daemon process.
   *
   * @param int $status
   *   The status to be set.
   * @param bool $log
   *   Whether to log this status change.
   */
  public function setStatus($status, $log = TRUE) {
    if ($this->status != $status) {
      $this->status = $status;
      // If we're reforking, store the reforked command.
      if ($status == StatusHandler::REFORKING) {
        $this->reforkedCommand = $this->daemon->getCommand();
        // Store the timestamp of this refork.
        $this->reforks[$this->reforkedCommand][] = time();
      }
      // Write the file, without re-reading from disk first.
      $this->writeStatusFile(FALSE);
      if ($log) {
        $this->logStatus('ok', "The @drush-cmd process state has changed to `@status`.");
      }
    }
    else {
      drush_log(dt('The status is already @status', array('@status' => $status)), 'warning');
    }
  }

  /**
   * Write this object to disk.
   *
   * @param bool $refresh_status
   *   If this is set to TRUE, the status will be re-read from disk before writing.
   */
  public function writeStatusFile($refresh_status = TRUE) {
    if ($refresh_status) {
      // Force a re-read of the status first, in case it has changed on disk.
      $this->getStatus(TRUE);
    }
    $file = fopen($this->statusFilename, 'w');
    fwrite($file, $this);
    fclose($file);
    $this->lastWritten = time();
    drush_log(dt('The status file has been written.'));
  }

  /**
   * Read the status file, and populate instance variables as needed. If instance
   * variables have already been set, they will not be overwritten by the file,
   * except for the case of status. We always want to read a fresh status in from
   * the file, in case another process has queued this for stopping or restarting.
   *
   * @return bool
   *   TRUE if the file was successfully read.
   */
  protected function readStatusFile() {
    // Just to be sure the file status is correct, clear out the stat cache.
    clearstatcache();
    if (file_exists($this->statusFilename) && $json = file_get_contents($this->statusFilename)) {
      $this->statusLastRead = time();

      foreach (json_decode($json, TRUE) as $key => $value) {
        // The initial read will set up all these values, but we don't want to
        // overwrite the variables with the ones on disk after the initial load,
        // unless it's the status variable, of course. :)
        if (!isset($this->$key) || $key == 'status') {
          $this->$key = $value;
        }
      }
      return TRUE;
    }
    return FALSE;
  }

  /**
   * Log the current status to drush.
   *
   * @param string $type
   *   The type of message to be logged. Common types are 'warning', 'error',
   *   'success' and 'notice'. Defaults to 'notice'.
   * @param string $message
   *   An optional custom message to log to be passed to dt(). You can use the
   *   @status placeholder.
   * @param array $t_args
   *   Custom placeholder arguments to be passed to dt().
   */
  public function logStatus($type = 'notice', $message = NULL, $t_args = NULL) {
    if (!isset($t_args)) {
      $t_args = array(
        '@status' => $this->getStatus(),
        '@drush-cmd' => $this->daemon->getDrushCommand(),
      );
    }
    if (!isset($message)) {
      $message = 'The @drush-cmd process is currently @status.';
    }

    drush_log(dt($message, $t_args), $type);
  }

  /**
   * Convert this object to json as a string.
   */
  public function __toString() {
    return json_encode($this);
  }
}