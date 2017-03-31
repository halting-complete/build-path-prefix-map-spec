#!/usr/bin/perl

use strict;
use warnings;

# Setting the variable

sub pfmap_enquote {
	my $part = shift;
	$part =~ s/%/%#/g;
	$part =~ s/=/%+/g;
	$part =~ s/:/%./g;
	return $part;
	}

sub pfmap_append($$) {
	my $dst = shift;
	my $src = shift;
	my $curmap = $ENV{"BUILD_PATH_PREFIX_MAP"};
	$ENV{"BUILD_PATH_PREFIX_MAP"} = ($curmap ? $curmap . ":" : "") .
		pfmap_enquote($dst) . "=" . pfmap_enquote($src);
}

pfmap_append($ENV{"NEWDST"}, $ENV{"NEWSRC"});

exec @ARGV;
