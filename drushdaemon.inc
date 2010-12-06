<?php

class DrushDaemon {
  /**
   * Process result constants.
   */
  const RESULT_PROCESSING  = 1; // Healthy process
  const RESULT_HIBERNATING = 2; // The process has interrupted itself (e.g., the
  const RESULT_INCOMPLETE  = 3; // The process has interrupted itself (e.g., the
                               // memory limit is approaching)
  const RESULT_STOPPED     = 4; // The process was stopped externally
  const RESULT_FAILED      = 5; // The process had a fatal error

  /**
   * Maximum memory threshold.
   *
   * @var float
   */
  const MEMORY_THRESHOLD = 0.85;

  /**
   * The daemon command, such as start, stop, restart.
   *
   * @var string
   */
  protected $command = '';
  public function getCommand() { return $this->command; }

  /**
   * Whether this is a daemon process or not.
   */
  protected $isDaemon;
  public function isDaemon() { return $this->isDaemon; }

  /**
   * The drush command.
   *
   * @var string
   */
  protected $drushCommand;
  public function getDrushCommand() { return $this->drushCommand; }

  /**
   * The drush command array.
   *
   * @var array
   */
  protected $drushCommandArray;

  /**
   * Frequency and frequency units, for logging.
   *
   * @var int
   * @var string
   */
  protected $frequency;
  protected $frequencyUnit;

  /**
   * Daemon commands that require a fork. This will probably only ever be 'start'.
   *
   * @var array
   */
  protected $forkingCommands = array('start');

  /**
   * The front end exit return value.
   *
   * @var int
   */
  protected $frontendReturn = 1;

  /**
   * Methods to call during health check.
   *
   * @var array
   */
  protected $healthChecks = array('checkMemoryUsage');

  /**
   * Duration of hibernation, in number of seconds.
   *
   * @var int
   */
  protected $hibernateDuration = 900;

  /**
   * The name of the callback function called to determing if there are items to
   * process.
   *
   * @var string
   */
  protected $needsProcessingCallback;

  /**
   * The output handler object.
   */
  protected $outputHandler;

  /**
   * Queue this process for reforking.
   *
   * @var bool
   */
  protected $refork = FALSE;
  public function refork() {
    $this->refork = TRUE;
    $this->setStatus(StatusHandler::REFORKING);
  }

  public function isReforking() {
    return ($this->getStatus() == StatusHandler::REFORKING) && ($this->statusHandler->getReforkedCommand() == $this->getCommand());
  }

  /**
   * The status handler object.
   */
  protected $statusHandler;

  /**
   * Duration in seconds that the status should reside in the static cache. When
   * this expires, the status is re-read from the status file.
   *
   * @var int
   */
  protected $statusTTL = 30;
  public function getStatusTTL() { return $this->statusTTL; }


  function __construct($command) {
    $this->drushCommandArray = $command;
    $this->setDaemon((boolean) drush_get_context('DRUSH_BACKEND'));
    $this->customizeOptions();
    $this->detectDrushCommand();
    $this->detectCommand();
    $this->detectOptions();
    $this->detectFilepaths();
    $this->statusHandler = new StatusHandler($this);
  }

  /**
   * Summon a daemon for this command, by forking.
   */
  public function summon() {
    $this->fork();
    drush_log('The daemon has started', 'success');
  }

  public function backendInvoke() {
    drush_set_context('DRUSH_LOG_CALLBACK', '_drushd_print_log');
    $this->setDaemon(TRUE);
    $this->outputHandler = new OutputHandler($this->logFilename);
    $this->outputHandler->start();

    try {
      $this->callCommandMethod();
    } catch (Exception $e) {
      drush_log(dt('Caught exception: @message', array('@message' => $e->getMessage())), 'error');
    }

    $this->outputHandler->end();
  }

  public function callCommandMethod() {
    $command = $this->command;
    $output = $this->$command();
    // If reforking is required, do it!
    if ($this->refork) {
      $this->fork();
    }
    return $output;
  }

  public function commandRequiresFork() {
    return in_array($this->command, $this->forkingCommands);
  }

  protected function fork() {
    $args = drush_get_arguments();
    $cmd = implode(' ', $args);
    $options = drush_get_context('options');
    $options['debug'] = TRUE;
    drush_backend_fork($cmd, $options);
  }



  protected function start() {
    if ($this->getStatus() == StatusHandler::STOPPED || $this->isReforking()) {
      $this->beginProcess();
      $this->iterateProcess();
      if ($this->getStatus() != StatusHandler::REFORKING) {
        $this->endProcess();
      }
    }
    else {
      $this->statusHandler->logStatus('error');
    }
  }

  protected function stop() {
    if (drush_get_option('kill', FALSE) && $this->statusHandler->getPid()) {
      $command = 'kill '. $this->statusHandler->getPid();
      // @todo, this needs some debugging.
      if (drush_shell_exec($command)) {
        $this->setStatus(StatusHandler::STOPPED, FALSE);
        $this->logStatus('success');
      }
      else {
        drush_log('Failed to kill the process', 'error');
      }
    }
    else {
      $log_type = 'error';
      $status = $this->getStatus(TRUE);
      if ($status == StatusHandler::STARTED || $status == StatusHandler::HIBERNATING) {
        $log_type = $this->_stop($status);
      }
      $this->statusHandler->logStatus($log_type);
    }
  }

  protected function status() {
    $this->statusHandler->logStatus('success');
    drush_log(dt('The process Id for this process is !pid', array('!pid' => $this->statusHandler->getPid())));
    if ($this->getStatus() == StatusHandler::HIBERNATING) {
      drush_log(dt('Last hibernation cycle was at @date', array('@date' => date("M d Y H:i:s", $this->statusHandler->getLastWritten()))), 'ok');
    }
  }

  /**
   * Helper to $this->stop(). Does all the dirty work of stopping and checking the
   * status and timeout, etc.
   *
   * @param string $status
   *   The StatusHandler status recorded before the stop command was called.
   * @return string
   *   The drush log type, 'success' or 'error'.
   */
  protected function _stop($status) {
    $queue = drush_get_option('queue', FALSE);
    $timeout = drush_get_option('timeout', 45);
    if ($status == StatusHandler::HIBERNATING && !$queue) {
      $delay = $this->hibernateDuration > 60 ? ceil($this->hibernateDuration / 60) .' minute' : $this->hibernateDuration .' second';
      drush_log(dt('This process is currently hibernating. Expect about a @delay delay.', array('@delay' => $delay)), 'warning');
      $opts = array("I'll wait around, thanks.", "Just queue it to stop and get me the heck out of here!");
      $choice = drush_choice($opts, dt('Watcha wanna do?'));
      if ($choice === 1) {
        $queue = TRUE;
      }
      else if ($choice === 0) {
        // Set the timeout to the hibernate duration, plus 30 seconds.
        $timeout = $this->hibernateDuration + 30;
      }
      else if ($choice === FALSE) {
        $this->frontendReturn = 0;
        $this->drushExit();
      }
    }
    else if ($timeout < $this->statusTTL) {
      drush_log(dt('The specified timeout is shorter than the status TTL of !num seconds.', array('!num' => $this->statusTTL)), 'warning');
    }

    $start = $time = time();

    $this->setStatus(StatusHandler::STOPPING);
    if (!$queue) {
      drush_log(dt('The status TTL is !num seconds, so expect to wait around at least that long.', array('!num' => $this->statusTTL)), 'ok');
      while ($this->getStatus(TRUE) == StatusHandler::STOPPING && $time - $start < $timeout) {
        $this->statusHandler->logStatus();
        // Sleep for 5 seconds.
        sleep(5);
        $time = time();
      }
      if ($time - $start >= $timeout) {
        drush_log(dt('The timeout of !num seconds was reached.', array('!num' => $timeout)), 'warning');
      }
    }

    // Get a fresh status. If we're just queuing to stop, we can't assume that
    // we get an error.
    return $queue || $this->getStatus(TRUE) == StatusHandler::STOPPED ? 'success' : 'error';
  }

  protected function restart() {
    
  }



  protected function beginProcess() {
    $this->statusHandler->SetStatus(StatusHandler::STARTED, FALSE);
    $this->statusHandler->logStatus('notice', 'The process has started.');
    $this->lastfeedback = microtime(TRUE);

    $this->total_processed = $this->total_successes =
      $this->processed_since_feedback = $this->successes_since_feedback = 0;
  }

  protected function iterateProcess() {
    $result = DrushDaemon::RESULT_PROCESSING;
    $process_result = TRUE;
    while ($result == DrushDaemon::RESULT_PROCESSING || $result == DrushDaemon::RESULT_HIBERNATING) {
      // This will write any errors, etc. to the log.
      if ($this->isDaemon()) {
        ob_flush();
      }

      // Only process if we're not hibernating.
      if (!$this->hibernate()) {
        $process_result = $this->process();
      }
      $result = $this->checkStatus($process_result);
    }
    $this->progressMessage($result);
  }

  /**
   * 
   */
  protected function hibernate() {
    $custom_callback = (isset($this->needsProcessingCallback) && function_exists($this->needsProcessingCallback)) ? $this->needsProcessingCallback : FALSE;
    $needs_processing = $custom_callback ? $custom_callback() : $this->needsProcessing();
    $hibernate = !$needs_processing;

    $status = $needs_processing ? StatusHandler::STARTED : StatusHandler::HIBERNATING;
    // Update the status if need be.
    if ($status != $this->getStatus()) {
      if ($status == StatusHandler::STARTED) {
        // If we're coming out of hibernation, reset the feedback the timer.
        $this->lastfeedback = microtime(TRUE);
      }
      $this->setStatus($status);
    }

    if ($hibernate) {
      sleep($this->hibernateDuration);
      // Do some file status cache clean up after coming out of hibernation.
      clearstatcache();
    }
    return $hibernate;
  }

  /**
   * If you are extending this class, you NEED to implement this method, or the
   * process will perpetually be in hibernation. If this method returns FALSE,
   * the process will go into hibernation.
   */
  protected function needsProcessing() {
    return FALSE;
  }

  protected function endProcess() {
    $this->statusHandler->SetStatus(StatusHandler::STOPPED, FALSE);
    $this->statusHandler->logStatus('notice', 'The process has stopped.');
  }



  protected function checkHealth() {
    foreach ($this->healthChecks as $function) {
      if ((method_exists($this, $function) && !$this->$function()) ||
        (function_exists($function) && !$function())) {
          $this->statusHandler->SetStatus(StatusHandler::STOPPING);
          return FALSE;
      }
    }

    return TRUE;
  }

  protected function checkStatus($process_result) {
    $healthy = $this->checkHealth();
    $status = $this->getStatus();
    if (!$healthy || !$process_result || $status == StatusHandler::STOPPING) {
      return DrushDaemon::RESULT_STOPPED;
    }
    if ($status == StatusHandler::REFORKING) {
      return DrushDaemon::RESULT_INCOMPLETE;
    }
    if ($status == StatusHandler::HIBERNATING) {
      return DrushDaemon::RESULT_HIBERNATING;
    }
    $result = DrushDaemon::RESULT_PROCESSING;
    $this->progressMessage($result);
    return $result;
  }

  /**
   * Here's the actual meat of the daemon, where each process occurs.
   */
  protected function process() {
    if (isset($this->drushCommandArray['daemon']['process callback'])) {
      $callback = $this->drushCommandArray['daemon']['process callback'];
      if (function_exists($callback)) {
        return $callback();
      }
      else {
        drush_log('No callback exists for this daemon process.', 'error');
        $this->stop();
      }
    }
    return FALSE;
  }

  /**
   * Outputs a progress message, reflecting the current status of a migration process.
   *
   * @param int $result
   *  Status of the process, represented by one of the MAGS_CUSTOM_RESULT_* constants.
   */
  protected function progressMessage($result) {
    // In the PROCESSING case, only proceed under the proper conditions
    if ($result == DrushDaemon::RESULT_PROCESSING) {
      if (isset($this->frequency)) {
        if (($this->frequencyUnit == 'seconds' && time()-$this->lastfeedback >= $this->frequency) ||
            ($this->frequencyUnit == 'items' && $this->processed_since_feedback >= $this->frequency)) {
          // Fall through
        }
        else {
          return;
        }
      }
      else {
        return;
      }
    }

    $time = microtime(TRUE) - $this->lastfeedback;
    if ($time > 0) {
      $perminute = round(60*$this->processed_since_feedback/$time);
      $time = round($time, 1);
    }
    else {
      $perminute = '?';
    }

    switch ($result) {
      case DrushDaemon::RESULT_PROCESSING:
        $basetext = "Processed !successes (!failed failed) in !time sec (!perminute/min) - done";
        $type = 'completed';
        break;
      case DrushDaemon::RESULT_FAILED:
        $basetext = "Processed !successes (!failed failed) in !time sec (!perminute/min) - failure";
        $type = 'failed';
        break;
      case DrushDaemon::RESULT_INCOMPLETE:
        $basetext = "Processed !successes (!failed failed) in !time sec (!perminute/min) - continuing";
        $type = 'ok';
        break;
      case DrushDaemon::RESULT_STOPPED:
        $basetext = "Processed !successes (!failed failed) in !time sec (!perminute/min) - stopped";
        $type = 'warning';
        break;
    }
    $message = t($basetext,
        array('!numitems' => $this->processed_since_feedback,
              '!successes' => $this->successes_since_feedback,
              '!failed' => $this->processed_since_feedback - $this->successes_since_feedback,
              '!time' => $time,
              '!perminute' => $perminute));
    drush_log($message, $type);

    // Report on lookup_cache hit rate. Only visible at 'debug' level.
    if ($result != DrushDaemon::RESULT_INCOMPLETE && !empty($this->counts['lookup_cache'])) {
      foreach ($this->counts['lookup_cache'] as $name => $tallies) {
        $tallies += array('hit' => 0, 'miss_hit' => 0, 'miss_miss' => 0); // Set defaults to avoid NOTICE.
        $sum = $tallies['hit']+$tallies['miss_hit']+$tallies['miss_miss'];
        drush_log(
          t('Lookup cache: !mn SM=!name !hit hit, !miss_hit miss_hit, !miss_miss miss_miss (!total total).', array(
                '!mn' => $this->machineName,
                '!name' => $name,
                '!hit' => round((100*$tallies['hit'])/$sum) . '%',
                '!miss_hit' => round((100*$tallies['miss_hit'])/$sum) . '%',
                '!miss_miss' => round((100*$tallies['miss_miss'])/$sum) . '%',
                '!total' => $sum
        )), 'debug');
      }
      $this->counts['lookup_cache'] = array();
    }
    if ($result == DrushDaemon::RESULT_INCOMPLETE) {
      $this->lastfeedback = time();
      $this->processed_since_feedback = $this->successes_since_feedback = 0;
    }
  }

  /**
   * Retrieve the current status of the daemon process.
   */
  public function getStatus($refresh = FALSE) {
    return $this->statusHandler->getStatus($refresh);
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
    $this->statusHandler->setStatus($status, $log);
  }

  public function getPid() {
    return $this->statusHandler->getPid();
  }

  protected function setDaemon($is_daemon = FALSE) {
    $this->isDaemon = (boolean) $is_daemon;
  }

  protected function customizeOptions() {
    if (isset($this->drushCommandArray['daemon']['options'])) {
      foreach ($this->drushCommandArray['daemon']['options'] as $name => $value) {
        $this->$name = $value;
      }
    }
  }

  protected function detectCommand() {
    $command = array_shift($this->drushCommandArray['arguments']);
    // Method will normally be one of 'start', 'stop', or 'restart'.
    if (method_exists($this, $command)) {
      $this->command = $command;
    }
  }

  protected function detectOptions() {
    if ($feedback = drush_get_option('feedback')) {
      $parts = explode(' ', $feedback);
      $this->frequency = $parts[0];
      $this->frequencyUnit = $parts[1];
      if ($this->frequencyUnit != 'seconds' && $this->frequencyUnit != 'items') {
        drush_set_error(NULL, dt("Invalid feedback frequency unit '!unit'",
          array('!unit' => $this->frequencyUnit)));
        return;
      }
    }
  }

  protected function detectFilepaths() {
    $path_aliases = _core_path_aliases();
    $filepath = $path_aliases['%root'] .'/'. $path_aliases['%files'];
    $name = 'drushd_'. str_replace('-', '_', $this->getDrushCommand());

    if (!isset($this->statusFilename)) {
      $filename = "$name.txt";
      $this->statusFilename = "$filepath/$filename";
    }
    if (!isset($this->logFilename)) {
      $filename = "$name.log";
      $this->logFilename = "$filepath/$filename";
    }
  }

  protected function detectDrushCommand() {
    $args = drush_get_arguments();
    $this->drushCommand = array_shift($args);
  }


  protected function checkMemoryUsage() {
    $memory_limit = DrushDaemon::getMemoryLimit();
    $usage = memory_get_peak_usage();
    $pct_memory = $usage/$memory_limit;
    if ($pct_memory > DrushDaemon::MEMORY_THRESHOLD) {
      $t_args = array(
        '!pct' => round($pct_memory*100),
        '!usage' => format_size($usage),
        '!limit' => format_size($memory_limit),
      );
      drush_log(dt('Memory usage is !usage (!pct% of limit !limit), starting new batch', $t_args), 'warning');
      $this->refork();
    }
    // This is not a fatal error, so always return TRUE.
    return TRUE;
  }

  static public function getMemoryLimit() {
    static $memory_limit;

    if (!isset($memory_limit)) {
      // Record the memory limit in bytes
      $limit = trim(ini_get('memory_limit'));
      $last = strtolower($limit[strlen($limit)-1]);
      switch ($last) {
        case 'g':
          $limit *= 1024;
        case 'm':
          $limit *= 1024;
        case 'k':
          $limit *= 1024;
          break;
        default:
          throw new Exception(t('Invalid PHP memory_limit !limit',
            array('!limit' => $limit)));
      }
      $memory_limit = $limit;
    }

    return $memory_limit;
  }

  public function drushExit() {
    drush_set_context('DRUSH_EXECUTION_COMPLETED', TRUE);
    exit($this->frontendReturn);
  }
}
