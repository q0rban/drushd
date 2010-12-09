<?php

class DrushDaemon {
  /**
   * Process result constants.
   */
  const RESULT_PROCESSING  = 1; // Healthy process
  const RESULT_HIBERNATING = 2; // The process is hibernating.
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
   * The front end exit return value. Zero is success.
   *
   * @var int
   */
  protected $frontendReturn = 0;

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
   * The file name and path of the log file.
   *
   * @var string
   */
  protected $logFilename;

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
   * Optional process callback function for non-OOP drush command processes.
   *
   * @var string
   */
  protected $processCallback;

  /**
   * The refork limit per minute, to protect against refork loops. This is the
   * number of allowable reforks per minute.
   *
   * @var int
   */
  protected $reforkLimitPerMin = 10;

  /**
   * Queue this process for reforking.
   *
   * @var bool
   */
  protected $refork = FALSE;

  /**
   * The file name and path of the status file.
   *
   * @var string
   */
  protected $statusFilename;

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

  /**
   * Constructor.
   *
   * @param $command
   *   The Drush command array.
   */
  function __construct($command) {
    $this->drushCommandArray = $command;
    $this->drushCommand = $command['command'];
    $this->isDaemon = (boolean) drush_get_context('DRUSH_BACKEND');
    $this->customizeOptions();
    $this->detectCommand();
    $this->detectOptions();
    $this->detectFilepaths();
    $this->statusHandler = new StatusHandler($this, $this->statusFilename);
  }



  /**
   * Command Relay
   * =============
   */

  /**
   * Summon a daemon for this command, by forking.
   */
  public function summon() {
    if ($this->canFork()) {
      $this->fork();
      $this->hasForked();
    }
  }

  /**
   * The command has been forked, and is now being called via the backend.
   */
  public function backendInvoke() {
    drush_set_context('DRUSH_LOG_CALLBACK', '_drushd_print_log');
    $this->isDaemon = TRUE;
    $this->outputHandler = new OutputHandler($this->logFilename);
    $this->outputHandler->start();

    try {
      $this->callCommandMethod();
    } catch (Exception $e) {
      drush_log(dt('Caught exception: @message', array('@message' => $e->getMessage())), 'error');
    }

    $this->outputHandler->end();
  }

  /**
   * Calls up the command method directly.
   *
   * @return mixed
   *   The command callback output.
   */
  public function callCommandMethod() {
    $command = $this->command;
    $output = $this->$command();
    // If reforking is required, do it!
    if ($this->refork) {
      $this->refork();
    }
    return $output;
  }

  /**
   * Fork the command
   */
  protected function fork() {
    $args = drush_get_arguments();
    $cmd = implode(' ', $args);
    $options = drush_get_context('options');
    drush_backend_fork($cmd, $options);
  }

  /**
   * Determine if this command can currently fork.
   */
  protected function canFork() {
    $method = $this->command .'CanFork';
    if (method_exists($this, $method)) {
      return $this->$method();
    }
    return TRUE;
  }

  /**
   * Forking has completed. Check to see if the command wants to do anything.
   */
  protected function hasForked() {
    $method = $this->command .'HasForked';
    if (method_exists($this, $method)) {
      $this->$method();
    }
  }

  /**
   * Refork the current command. Ensures that reforking doesn't exceed certian
   * limits per minute.
   */
  protected function refork() {
    // Let's make sure we're not in a reforking loop first.
    if (isset($this->statusHandler->reforks[$this->command])) {
      $reforks = $this->statusHandler->reforks[$this->command];
      $count = count($reforks);
      $first = array_shift($reforks);
      $last = array_pop($reforks);
      $duration = ($last - $first) / 60;

      if ($count >= $this->reforkLimitPerMin) {
        // If we've exceeded at our refork limit, and it's less than a minute,
        // throw an error and stop the process.
        if ($duration < 1) {
          drush_log(dt('The refork limit has been exceeded for this process. Please check the memory requirements and available memory and try again.'), 'error');
          $this->setStatus(StatusHandler::STOPPED);
          $this->frontendReturn = 1;
          return;
        }
        else {
          // No need to store more reforks than our limit.
          $keepers = array_slice($this->statusHandler->reforks[$this->command], - $this->reforkLimitPerMin);
          $this->statusHandler->reforks[$this->command] = $keepers;
          $this->statusHandler->writeStatusFile();
        }
      }
    }
    $this->fork();
  }

  /**
   * Queue this process to refork.
   */
  public function queueRefork() {
    $this->refork = TRUE;
    $this->setStatus(StatusHandler::REFORKING);
  }

  /**
   * Check to see if this process is currently reforking.
   */
  public function isReforking() {
    return ($this->getStatus() == StatusHandler::REFORKING) && ($this->statusHandler->getReforkedCommand() == $this->getCommand());
  }


  /**
   * Command Callbacks
   * =================
   */

  /**
   * Start up the process. This is a forking command.
   */
  protected function start() {
    if ($this->getStatus() == StatusHandler::STOPPED || $this->isReforking()) {
      $this->_start();
    }
    else {
      $this->statusHandler->logStatus('error');
    }
  }

  /**
   * Check to make sure the process can be started as a fork.
   */
  protected function startCanFork() {
    if ($this->getStatus() == StatusHandler::STOPPED) {
      return TRUE;
    }
    $this->statusHandler->logStatus('error');
    return FALSE;
  }

  /**
   * Starting has forked, provide frontend details about the success thereof.
   */
  protected function startHasForked() {
    $log_type = $this->queueStatusChange(StatusHandler::STOPPED, StatusHandler::STOPPED, StatusHandler::STARTED);
    // Hibernating is an ok status also.
    if ($this->getStatus() == StatusHandler::HIBERNATING) {
      $log_type = 'success';
    }
    $this->statusHandler->logStatus($log_type);
  }
  

  // @todo: This is currently unsafe, as it could lead to multiple commands
  // running at the same time.
  /**
   * Resume the process. This is a forking command.
   *
  protected function resume() {
    $this->_start();
  }

  /**
   * Helper to resume and start.
   */
  protected function _start() {
    $this->beginProcess();
    $this->iterateProcess();
    if ($this->getStatus() != StatusHandler::REFORKING) {
      $this->endProcess();
    }
  }

  /**
   * Return a status about the current process.
   */
  protected function status() {
    $this->statusHandler->logStatus('success');
    if ($pid = $this->statusHandler->getPid()) {
      drush_log(dt('The process Id for this process is !pid', array('!pid' => $pid)), 'ok');
    }
    if ($this->getStatus() == StatusHandler::HIBERNATING) {
      drush_log(dt('Last hibernation cycle was at @date.', array('@date' => date("H:i:s", $this->statusHandler->getLastWritten()))), 'ok');
    }
    if (isset($this->statusHandler->memoryPeakUsage)) {
      $dt_args = array(
        '!peak' => $this->statusHandler->memoryPeakUsage,
        '!limit' => format_size($this->getMemoryLimit())
      );
      drush_log(dt('The last recorded peak memory usage was !peak of limit !limit', $dt_args), 'ok');
    }
    if (drush_get_context('DRUSH_VERBOSE')) {
      drush_print($this->statusHandler);
    }
  }

  /**
   * Shows the log file, using less.
   */
  protected function showLog() {
    if (isset($this->logFilename) && file_exists($this->logFilename)) {
      $lines = drush_get_option('lines', 0);
      if (drush_get_option('watch')) {
        $command  = 'watch tail';
        $command .= $lines ? " -n $lines " : ' ';
        $command .= $this->logFilename;
      }
      else if (drush_get_option('tail')) {
        $command  = 'tail';
        $command .= $lines ? " -n $lines " : ' ';
        $command .= $this->logFilename;
      }
      else {
        $command = "less +G {$this->logFilename}";
      }
      drush_log(dt('Executing !command', array('!command' => $command)));
      proc_close(proc_open($command, array(0 => STDIN, 1 => STDOUT, 2 => STDERR), $pipes));
    }
    else {
      drush_log(dt('No log file exists for this process.'), 'error');
    }
  }

  /**
   * Stop the process.
   */
  protected function stop() {
    $log_type = 'error';
    $status = $this->getStatus(TRUE);
    if ($status == StatusHandler::STARTED || $status == StatusHandler::HIBERNATING || $status == StatusHandler::REFORKING) {
      $log_type = $this->queueStatusChange($status, StatusHandler::STOPPING, StatusHandler::STOPPED);
    }
    $this->statusHandler->logStatus($log_type);
  }

  /**
   * Kill the process immediately
   */
  protected function kill() {
    if ($pid = $this->statusHandler->getPid()) {
      $command = 'kill '. $pid;

      if (drush_shell_exec($command)) {
        $this->setStatus(StatusHandler::STOPPED, FALSE);
        $this->statusHandler->logStatus('success');
      }
      else {
        drush_log('Failed to kill the process', 'error');
      }
    }
    else {
      $message = function_exists('posix_getpid') ? 'No process Id was found.'
        : 'The PHP function posix_getpid() does not exist on this server.';
      drush_log(dt($message), 'error');
    }
  }

  /**
   * Restart the process by reforking.
   */
  protected function restart() {
    $log_type = 'error';
    $status = $this->getStatus(TRUE);
    if ($status == StatusHandler::STARTED || $status == StatusHandler::HIBERNATING) {
      $log_type = $this->queueStatusChange($status, StatusHandler::REFORKING, StatusHandler::STARTED);
    }
    $this->statusHandler->logStatus($log_type);
  }

  /**
   * Helper to queue a change in status. Does all the dirty work of checking the
   * status and timeout, etc. Both 'restart' and 'stop' commands call this method,
   * so check there for usage.
   *
   * @param string $current_status
   *   The StatusHandler status recorded before the method was called.
   * @param string $transition_status
   *   The status that queues the change, e.g. STOPPING, REFORKING
   * @param string $desired_status
   *   The desired status to achieve, for instance STOPPED, or STARTED.
   * @return string
   *   The drush log type, 'success' or 'error'.
   */
  protected function queueStatusChange($current_status, $transition_status, $desired_status) {
    $queue = drush_get_option('queue', FALSE);
    $timeout = drush_get_option('timeout', 45);
    if ($current_status == StatusHandler::HIBERNATING && !$queue) {
      $delay = $this->hibernateDuration > 60 ? ceil($this->hibernateDuration / 60) .' minute' : $this->hibernateDuration .' second';
      drush_log(dt('This process is currently hibernating. Expect about a @delay delay.', array('@delay' => $delay)), 'warning');
      $opts = array("I'll wait around, thanks.", "Just queue it up and get me the heck out of here!");
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

    if ($current_status != $transition_status) {
      $this->setStatus($transition_status);
      // Give it a sec to save the file. :)
      sleep(1);
    }

    $start = $time = time();

    if (!$queue) {
      drush_log(dt('The status TTL is !num seconds.', array('!num' => $this->statusTTL)));
      while ($this->getStatus(TRUE) == $transition_status && $time - $start < $timeout) {
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
    return $queue || $this->getStatus(TRUE) == $desired_status ? 'success' : 'error';
  }


  /**
   * Process Handling Functions
   * ==========================
   */

  /**
   * Sets up the process for starting.
   */
  protected function beginProcess() {
    $this->statusHandler->SetStatus(StatusHandler::STARTED, FALSE);
    $this->statusHandler->logStatus('ok', 'The @drush-cmd process has started.');
    $this->lastfeedback = microtime(TRUE);

    $this->total_processed = $this->total_successes =
      $this->processed_since_feedback = $this->successes_since_feedback = 0;
  }

  /**
   * Here's where the actual processing occurs.
   */
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
   * If you are extending this class, you NEED to implement this method, or the
   * process will perpetually be in hibernation. If this method returns FALSE,
   * the process will go into hibernation.
   */
  protected function needsProcessing() {
    return FALSE;
  }

  /**
   * Here's where each process occurs. If you are extending this class, you will
   * likely want to implement this method. Here it's just calling a separate
   * process function.
   */
  protected function process() {
    if (isset($this->processCallback)) {
      $callback = $this->processCallback;
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
   * There is no processing currently needed, so go to sleep.
   */
  protected function hibernate() {
    $custom_callback = (isset($this->needsProcessingCallback) && function_exists($this->needsProcessingCallback)) ? $this->needsProcessingCallback : FALSE;
    $needs_processing = $custom_callback ? $custom_callback() : $this->needsProcessing();
    $hibernate = !$needs_processing;

    $status = $needs_processing ? StatusHandler::STARTED : StatusHandler::HIBERNATING;
    $current_status = $this->getStatus();
    // Update the status if need be, but don't do it if we're stopping.
    if ($status != $current_status && $current_status != StatusHandler::STOPPING) {
      if ($status == StatusHandler::STARTED) {
        // If we're coming out of hibernation, reset the feedback timer.
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
   * Ends the current process.
   */
  protected function endProcess() {
    $this->statusHandler->SetStatus(StatusHandler::STOPPED, FALSE);
    $this->statusHandler->logStatus('ok', 'The process has stopped.');
  }


  /**
   * Health Checks and Progress Messages
   * ===================================
   */

  /**
   * Ensures the status still allows for continued processing.
   */
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
   * Iterates through all health check callbacks, ensuring they all return TRUE.
   */
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

  /**
   * Health check to check memory usage. If memory exceeds the threshold, the
   * process will refork.
   */
  protected function checkMemoryUsage() {
    $memory_limit = DrushDaemon::getMemoryLimit();
    $usage = memory_get_peak_usage();
    $this->statusHandler->memoryPeakUsage = format_size($usage);
    $pct_memory = $usage/$memory_limit;
    if ($pct_memory > DrushDaemon::MEMORY_THRESHOLD) {
      $t_args = array(
        '!pct' => round($pct_memory*100),
        '!usage' => $this->statusHandler->memoryPeakUsage,
        '!limit' => format_size($memory_limit),
      );
      drush_log(dt('Memory usage is !usage (!pct% of limit !limit), reforking.', $t_args), 'warning');
      $this->queueRefork();
    }
    // This is not a fatal error, so always return TRUE.
    return TRUE;
  }

  /**
   * Outputs a progress message, reflecting the current status of a migration process.
   *
   * @param int $result
   *   Status of the process, represented by one of the DrushDaemon::RESULT_* constants.
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
    $failed = $this->processed_since_feedback - $this->successes_since_feedback;

    $message = "Processed !successes!failed in !time sec (!perminute/min)";
    switch ($result) {
      case DrushDaemon::RESULT_FAILED:
        $message .= " - failure";
        $type = 'failed';
        break;
      case DrushDaemon::RESULT_PROCESSING:
      case DrushDaemon::RESULT_INCOMPLETE:
        $message .= " - continuing";
        $type = 'ok';
        break;
      case DrushDaemon::RESULT_STOPPED:
        $message .= " - stopped";
        $type = 'warning';
        break;
      case DrushDaemon::RESULT_HIBERNATING:
        $message .= " - completed";
        $type = 'success';
        break;
    }

    $dt_args = array(
      '!numitems' => $this->processed_since_feedback,
      '!successes' => $this->successes_since_feedback,
      '!failed' => $failed > 0 ? dt(' (!failed failed)', array('!failed' => $failed)) : '',
      '!time' => $time,
      '!perminute' => $perminute,
    );

    drush_log(dt($message, $dt_args), $type);

    if ($result == DrushDaemon::RESULT_PROCESSING) {
      $this->lastfeedback = time();
      $this->processed_since_feedback = $this->successes_since_feedback = 0;
    }
  }


  /**
   * Helper functions and Misc. Getters
   * ==================================
   */

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

  /**
   * Get the process ID.
   */
  public function getPid() {
    return $this->statusHandler->getPid();
  }

  /**
   * Loads up the daemon options from hook_drush_command, setting them on the object.
   */
  protected function customizeOptions() {
    if (isset($this->drushCommandArray['daemon']['options'])) {
      foreach ($this->drushCommandArray['daemon']['options'] as $name => $value) {
        $this->$name = $value;
      }
    }
  }

  /**
   * Detects the command that has been called, be it 'start', 'stop', 'restart', etc.
   */
  protected function detectCommand() {
    $first_argument = array_shift($this->drushCommandArray['arguments']);
    // Strip all hyphens, for instance show-log calls $DrushDaemon::showLog().
    $command = str_replace('-', '', $first_argument);
    // Method will normally be one of 'start', 'stop', or 'restart'.
    if (method_exists($this, $command)) {
      $this->command = $command;
    }
  }

  /**
   * Detects Drush options, setting up instance variables where needed.
   */
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

  /**
   * Detects safe filepaths for Logging and Status files.
   */
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

  /**
   * Gets the current PHP memory limit.
   */
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

  /**
   * Determine if this command requires a fork (to be run in the background).
   *
   * @see drushd_drush_init().
   *
   * @return bool
   *   TRUE if the command requires background processing.
   */
  public function commandRequiresFork() {
    return in_array($this->command, $this->forkingCommands);
  }

  /**
   * Exits out of Drush cleanly, preventing command callbacks from being called.
   */
  public function drushExit() {
    drush_set_context('DRUSH_EXECUTION_COMPLETED', TRUE);
    exit($this->frontendReturn);
  }
}
