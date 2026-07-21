#!/bin/sh
#
# run-bounded.sh SECONDS "phase name" cmd [args...]
#
# Bound one macOS packaging phase in wall-clock time and fail LOUDLY on expiry.
#
# Why this exists: the packaging step blocked a CI job for 21 minutes with ZERO
# output and had to be cancelled by hand — twice (runs 29857249811, 29860430214).
# A build step that can hang unbounded is a defect in its own right: the job
# never fails, it just stops, and no log is available until it ends. Every phase
# is bounded so a hang becomes a fast, specific, named failure.
#
# macOS ships no GNU coreutils `timeout`. /usr/bin/perl is in the macOS base
# system, so the alarm is done there.
#
# setpgrp + kill on the process GROUP is load-bearing: the child may itself have
# children (jnext runs `system("ffmpeg -version")` at startup), and killing only
# the leader leaves the job blocked on the inherited pipe.
#
exec /usr/bin/perl -e '
  my ($t, $phase, @cmd) = @ARGV;
  my $pid = fork; die "run-bounded: fork: $!" unless defined $pid;
  if (!$pid) { setpgrp(0,0); exec { $cmd[0] } @cmd or die "run-bounded: exec $cmd[0]: $!"; }
  $SIG{ALRM} = sub {
      kill("KILL", -$pid); waitpid($pid, 0);
      print STDERR "\n*** TIMEOUT: macOS packaging phase [$phase] exceeded ${t}s -- killed.\n";
      print STDERR "*** command was: @cmd\n";
      exit 124;
  };
  alarm $t; waitpid($pid, 0); alarm 0;
  my $st = $?;
  if ($st & 127) {
      printf STDERR "*** macOS packaging phase [%s] died on signal %d\n", $phase, $st & 127;
      exit 128 + ($st & 127);
  }
  my $rc = $st >> 8;
  print STDERR "*** macOS packaging phase [$phase] failed (exit $rc)\n" if $rc;
  exit $rc;
' "$@"
