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
  protected $lastWritten;
  public function getLastWritten() { return $this->lastWritten; }

  /**
   * The process ID.
   *
   * @var int
   */
  protected $pid;
  public function getPid() { return $this->pid; }

  /**
   * The command that was reforked.
   *
   * @var string
   */
  protected $reforkedCommand = '';
  public function getReforkedCommand() { return $this->reforkedCommand; }

  /**
   * The current status.
   *
   * @var string
   */
  static protected $status;

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



  public function __construct(DrushDaemon $daemon) {
    $this->daemon = $daemon;
    $this->statusTTL = $daemon->getStatusTTL();
    $this->detectStatusFilename();
    $this->detectStatus();
  }

  protected function detectStatusFilename() {
    $path_aliases = _core_path_aliases();

    $name = str_replace('-', '_', $this->daemon->getDrushCommand());

    $filepath = $path_aliases['%root'] .'/'. $path_aliases['%files'];
    $filename = 'drushd_'. $name .'.txt';
    $fullpath = $filepath .'/'. $filename;

    $this->statusFilename = $fullpath;
  }

  protected function detectStatus() {
    if ($items = $this->readStatusFile()) {
      $this->statusLastRead = time();
      $this->status = $items['status'];
      $this->lastWritten = $items['last_written'];
      $this->pid = isset($items['pid']) ? $items['pid'] : NULL;
      $this->reforkedCommand = isset($items['reforked_command']) ? $items['reforked_command'] : NULL;
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
    $this->status = $status;
    $this->writeStatusFile();
    if ($log) {
      $this->logStatus();
    }
  }

  protected function writeStatusFile() {
    // Only save a new pid if this is the daemonized process.
    $items = $this->daemon->isDaemon() ? array('pid' => posix_getpid()) : $this->readStatusFile();
    $items['status'] = $this->status;
    $items['last_written'] = $this->lastWritten = time();

    // If we're reforking, store the reforked command.
    if ($this->status == StatusHandler::REFORKING) {
      $items['reforked_command'] = $this->daemon->getCommand();
    }

    $json = json_encode($items);

    $file = fopen($this->statusFilename, 'w');
    fwrite($file, $json);
    fclose($file);
  }

  protected function readStatusFile() {
    // Just to be sure the file status is correct, clear out the stat cache.
    clearstatcache();
    if (file_exists($this->statusFilename) && $json = file_get_contents($this->statusFilename)) {
      return json_decode($json, TRUE);
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
}