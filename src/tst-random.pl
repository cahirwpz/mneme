#!/usr/bin/perl

use strict;
use warnings;

my $i = 1;
my $t = 100000;
my $n = 0;

while ($i <= 1000) { 
	my $cmd = "tst-random -s $i -c $t";

	system("./$cmd -n 1 -t 0 2>/dev/null >/dev/null");

	print "$cmd ... ";

	if ($? != 0) {
		print "failed!\n";
		$n++;
	} else {
		print "passed.\n";
	}

	die if ($n > 5);

	$i += 1;
}
