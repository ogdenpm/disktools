sub showMap {
    my ($spt, $interleave) = @_;
    my $slot = 0;
    my @imap, @smap;
    for (my $i = 0; $i < $spt; $i++) {
        while ($imap[$slot] != 0) {
            $slot = ($slot + 1) % $spt;
        }
        $imap[$slot] = $i + 1;
        $smap[$i] = $slot;
        $slot = ($slot + $interleave) % $spt;
    }
    printf "%02d - %02d:", $spt, $interleave;
    for (my $i = 0; $i < $spt; $i++) {
        printf " %02d", $imap[$i];
    }
    print "\n";
    
#    print "$interleave: ", join(" ", @imap), "\n";
#    $smap[$spt] = $smap[0];
#    for (my $i = 0; $i < $spt; $i++) {
#        if ($smap[$i+1] > $smap[$i]) {
#            printf "%02d ", ($smap[$i+1] - $smap[$i]);
#        } else {
#            printf "%02d ", ($smap[$i+1]+$spt - $smap[$i]);
#        }
#    }
#    print "\n";
}

for (my $i = 1; $i < 13; $i++) {
    showMap(52, $i);
}
for (my $i = 1; $i < 13; $i++) {
    showMap(30, $i);
}
for (my $i = 1; $i < 13; $i++) {
    showMap(26, $i);
}
for (my $i = 1; $i < 13; $i++) {
    showMap(16, $i);
}
for (my $i = 1; $i < 13; $i++) {
    showMap(15, $i);
}
