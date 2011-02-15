----------------
GET IT INSTALLED
================

To install the node access rebuilding daemon, do the following:

    # Make the .drush directory in your user's home if it doesn't exist.
    mkdir ~/.drush
    cd ~/.drush
    git clone git://github.com/q0rban/drushd.git

Then, run this command to start the node access rebuild daemon.

    # The command in its simplest form, but you may prefer one of these other
    # options, below.
    drush nar start
    # To run the command, logging feedback every 60 seconds.
    drush nar start --feedback="60 seconds"
    # To run the command, with verbose logging and feedback.
    drush nar start --verbose --feedback="100 items"

-------------------
SEE WHATS HAPPENING
===================

Keep in mind, if node access permissions do not need to be rebuilt, the daemon
will go into hibernation, checking node access rebuild status every 15 minutes
or so. To see the current status of the node access rebuild process, run:

    drush nar status

If you'd like to manually trigger node access rebuilding, run this command:

    drush node-access-needs-rebuild 1

If the daemon is hibernating, it may take 15 minutes for rebuilding to start.
Once it has started, you can watch the progress by running:

    drush nar show-log --watch

CTRL-C out of this command when you are done watching. Keep in mind, if you
didn't specify any feedback, you won't see much happening in the log. If you'd
like to just see the end of the log, run:

    drush nar show-log --tail

To browse the whole log file, run:

    drush nar show-log

-------------------
STOPPING THE DAEMON
===================

To stop the daemon, run:

    drush nar stop

If it's hibernating, it will still have to go through the whole 15 minute cycle
before the daemon will stop. You can queue it up to happen behind the scenes, or
wait around for it to stop.

If PHP has the posix_get_pid() function available, you can also force kill the
process by running:

    # Use with caution, you'll have to delete the status file in order to
    # restart the daemon.
    drush nar kill

--------------------
OTHER THINGS OF NOTE
====================

The status file and log are saved to the drupal files directory. They will be
named drushd_node_access_rebuild.log and drushd_node_access_rebuild.txt.

If you would like to store these files in a separate directory, it is recommended
to use a drushrc.php file to do so. Inside the drushrc.php file, add an adaptation
of the following:

    <?php
    $command_specific['nar'] = array(
      // Save all files to a log directory.
      'filepath' => '/var/www/html/example.com/log',
      // Optionally, specify the exact filenames:
      //'logfile' => '/var/log/example-com-nar.log',
      //'statusfile' => '/var/run/example-com-nar.status',
    );
    ?>

Keep in mind, the user running the command will need write privileges to those
files.


