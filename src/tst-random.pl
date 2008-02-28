#!/usr/bin/perl

use strict;
use warnings;

my $i = 1;
my $t = 10000;

while ($i <= 1000) { 
	print "tst-random $i $t\n";

	system("./tst-random -s $i -c $t -n 5 -t 2 2>/dev/null");

	die if ($? != 0);

	$i += 1;
}
