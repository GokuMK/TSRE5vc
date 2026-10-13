#!/usr/bin/perl
# Survey of MSTS .pat files against their route's track database
# (task editor 06, "Reference: MSTS paths").
#
# Usage: perl patscan.pl [ROUTES directory] [route ...] > results.txt
#        V=1 perl patscan.pl ...   also lists the ambiguous and gap cases.
# Defaults: the MSTS ROUTES directory below and the original MSTS routes.
# The main routes of junctions come from GLOBAL/tsection.dat next to ROUTES.
#
# "[broken path]" marks counts from paths with a node off the current track
# database (a junction node 1 m or more from any junction, or a point 2 m or
# more from track).
use strict; use warnings;
use Encode qw(decode);

my $root = shift // '/c/MagiPacks/Microsoft Train Simulator/ROUTES';
my @routes = @ARGV ? @ARGV : ('EUROPE1','EUROPE2','JAPAN1','JAPAN2','USA1','USA2','TUTORIAL ROUTE');
my $NONE = 4294967295;
my $verbose = $ENV{V} // 0;

sub slurp {
    my ($f) = @_;
    open my $h, '<:raw', $f or return undef;
    local $/; my $b = <$h>; close $h;
    if ($b =~ /^\xFF\xFE/) { return decode('UTF-16LE', substr($b, 2)); }
    if ($b =~ /^\xFE\xFF/) { return decode('UTF-16BE', substr($b, 2)); }
    return $b;
}

sub tokens {
    my ($t) = @_;
    my @tok;
    while ($t =~ /\G\s*(?:(\()|(\))|"((?:[^"\\]|\\.)*)"|([^\s()"]+))/gc) {
        push @tok, defined $1 ? '(' : defined $2 ? ')' : defined $3 ? "\"$3" : $4;
    }
    return \@tok;
}

# Tree: [name, [children/values...]]
sub parse {
    my ($tok, $i) = @_;
    my @items;
    while ($$i < @$tok) {
        my $t = $tok->[$$i];
        if ($t eq ')') { $$i++; return \@items; }
        if ($$i + 1 < @$tok && $tok->[$$i + 1] eq '(') {
            $$i += 2;
            push @items, [lc $t, parse($tok, $i)];
            next;
        }
        if ($t eq '(') { $$i++; push @items, ['', parse($tok, $i)]; next; }
        push @items, $t; $$i++;
    }
    return \@items;
}

sub find { my ($items, $name) = @_; for (@$items) { return $_->[1] if ref $_ && $_->[0] eq $name; } return undef; }
sub findall { my ($items, $name) = @_; return grep { ref $_ && $_->[0] eq $name } @$items; }

my %tot;
# tsection.dat track shapes: index -> main route.
my %mainRoute;
{
    my $ts = parse(tokens(slurp("$root/../GLOBAL/tsection.dat")), \(my $it = 0));
    my $shapes = find($ts, 'trackshapes') // [];
    for my $s (findall($shapes, 'trackshape')) {
        my $m = find($s->[1], 'mainroute');
        $mainRoute{$s->[1][0]} = $m ? $m->[0] : 0;
    }
}
our %N;
# Whether a vector node leaves junction $j by its main route; undef when $j does not diverge to it.
sub isMain {
    my ($j, $v) = @_;
    my $p = $N{$j}{pins};
    for my $i (1 .. $#$p) {
        next unless $p->[$i] == $v;
        return ($i == 1 + ($mainRoute{$N{$j}{shape}} // 0)) ? 1 : 0;
    }
    return undef;
}
sub cnt { $tot{$_[0]} += ($_[1] // 1); }

for my $route (@routes) {
    my $rd = "$root/$route";
    opendir my $dh, $rd or next;
    my ($tdbf) = grep { /\.tdb$/i } readdir $dh; closedir $dh;
    next unless $tdbf;
    my $tdb = parse(tokens(slurp("$rd/$tdbf")), \(my $i0 = 0));
    my $trackdb = find($tdb, 'trackdb');
    my $nodes = find($trackdb, 'tracknodes');
    %N = ();   # id -> {type, pos [wx,y,wz], pins [links], poly [[wx,wz],...]}
    for my $tn (findall($nodes, 'tracknode')) {
        my $c = $tn->[1];
        my $id = $c->[0];
        my %n;
        if (find($c, 'trendnode')) { $n{type} = 'end'; }
        elsif (my $j = find($c, 'trjunctionnode')) { $n{type} = 'junction'; $n{shape} = $j->[1]; }
        elsif (my $v = find($c, 'trvectornode')) {
            $n{type} = 'vector';
            my $s = find($v, 'trvectorsections');
            my $k = $s->[0];
            for my $q (0 .. $k - 1) {
                my @f = @$s[1 + $q * 16 .. 16 + $q * 16];
                push @{$n{poly}}, [$f[8] * 2048 + $f[10], $f[9] * 2048 + $f[12]];
            }
        }
        if (my $u = find($c, 'uid')) { $n{pos} = [$u->[4] * 2048 + $u->[6], $u->[7], $u->[5] * 2048 + $u->[8]]; }
        if (my $p = find($c, 'trpins')) { $n{pins} = [map { $_->[1][0] } findall($p, 'trpin')]; }
        $N{$id} = \%n;
    }
    # Close vector polylines with their end junction, and index connections.
    my %between;  # "a,b" -> [vector ids]
    for my $id (keys %N) {
        my $n = $N{$id};
        next unless $n->{type} eq 'vector';
        my ($pa0, $pb0) = @{$n->{pins}};
        push @{$n->{poly}}, [$N{$pb0}{pos}[0], $N{$pb0}{pos}[2]] if $N{$pb0} && $N{$pb0}{pos};
        unshift @{$n->{poly}}, [$N{$pa0}{pos}[0], $N{$pa0}{pos}[2]] if $N{$pa0} && $N{$pa0}{pos};
        push @{$between{join q(,), sort { $::a <=> $::b } $pa0, $pb0}}, $id;
    }
    my @jn = grep { $N{$_}{type} ne 'vector' } keys %N;
    my @vn = grep { $N{$_}{type} eq 'vector' } keys %N;

    my $segdist = sub {
        my ($px, $pz, $ax, $az, $bx, $bz) = @_;
        my ($dx, $dz) = ($bx - $ax, $bz - $az);
        my $l = $dx * $dx + $dz * $dz;
        my $t = $l > 0 ? (($px - $ax) * $dx + ($pz - $az) * $dz) / $l : 0;
        $t = 0 if $t < 0; $t = 1 if $t > 1;
        my ($cx, $cz) = ($ax + $t * $dx - $px, $az + $t * $dz - $pz);
        return sqrt($cx * $cx + $cz * $cz);
    };
    my $nearestJ = sub {
        my ($x, $z) = @_; my ($best, $bd) = (undef, 1e9);
        for (@jn) { my $p = $N{$_}{pos} or next; my $d = sqrt(($p->[0] - $x) ** 2 + ($p->[2] - $z) ** 2); ($best, $bd) = ($_, $d) if $d < $bd; }
        return ($best, $bd);
    };
    my $nearestV = sub {
        my ($x, $z) = @_; my ($best, $bd) = (undef, 1e9);
        for my $v (@vn) { my $pl = $N{$v}{poly}; for my $k (0 .. $#$pl - 1) {
            my $d = $segdist->($x, $z, @{$pl->[$k]}, @{$pl->[$k + 1]}); ($best, $bd) = ($v, $d) if $d < $bd; } }
        return ($best, $bd);
    };

    opendir my $ph, "$rd/PATHS" or next;
    my @pats = sort grep { /\.pat$/i } readdir $ph; closedir $ph;
    for my $pf (@pats) {
        my $txt = slurp("$rd/PATHS/$pf") // next;
        my $t = parse(tokens($txt), \(my $i1 = 0));
        my $pdpsT = find($t, 'trackpdps') // next;
        my @pdp = map { $_->[1] } findall($pdpsT, 'trackpdp');
        my $tp = find($t, 'trackpath');
        my $nodesT = find($tp, 'trpathnodes');
        my @pn = map { $_->[1] } findall($nodesT, 'trpathnode');
        my $pflags = find($tp, 'trpathflags');
        cnt('paths'); cnt("route $route paths");
        cnt('TrPathFlags ' . ($pflags ? $pflags->[0] : 'absent'));
        my $name = "$route/$pf";
        # Resolve each node to the TDB.
        my @res;
        my %pdpUse;
        for my $k (0 .. $#pn) {
            my ($fl, $nx, $nx2, $pi) = @{$pn[$k]};
            my $p = $pdp[$pi];
            $pdpUse{$pi}++;
            my ($x, $z) = ($p->[0] * 2048 + $p->[2], $p->[1] * 2048 + $p->[4]);
            my $pf2 = "$p->[5] $p->[6]";
            my $fh = hex($fl);
            my $pos = $k == 0 ? 'first' : ($nx == $NONE ? 'last' : 'mid');
            my $pk = "PDP $pf2 | node flags " . sprintf("%08x", $fh) . " | $pos";
            cnt('passing link') if $nx2 != $NONE;
            my %r = (pk => $pk, k => $k, pdpf => $p->[5], fl => $fh, nx => $nx, nx2 => $nx2);
            if ($p->[5] == 2) {
                my ($j, $d) = $nearestJ->($x, $z);
                $r{j} = $j; $r{jd} = $d;
                cnt($d < 1 ? "junction PDP on TDB $N{$j}{type}" : $d < 10 ? 'junction PDP 1-10 m off' : 'junction PDP >10 m off');
                printf "  far junction %s node %d %.1f m\n", $name, $k, $d if $d >= 1 && $verbose;
            } else {
                my ($v, $d) = $nearestV->($x, $z);
                my ($j, $dj) = $nearestJ->($x, $z);
                $r{v} = $v; $r{vd} = $d;
                { my $pl = $N{$v}{poly}; my ($acc, $tot, $best, $bd) = (0, 0, 0, 1e9);
                  for my $s (0 .. $#$pl - 1) { my $l = sqrt(($pl->[$s+1][0]-$pl->[$s][0])**2 + ($pl->[$s+1][1]-$pl->[$s][1])**2);
                    my $dd = $segdist->($x, $z, @{$pl->[$s]}, @{$pl->[$s+1]});
                    if ($dd < $bd) { $bd = $dd; $best = $tot + sqrt((($x-$pl->[$s][0])**2 + ($z-$pl->[$s][1])**2) - $dd*$dd < 0 ? 0 : (($x-$pl->[$s][0])**2 + ($z-$pl->[$s][1])**2) - $dd*$dd); }
                    $tot += $l; }
                  $r{frac} = $tot > 0 ? $best / $tot : 0; $r{frac} = 0.999 if $r{frac} >= 1; }
                cnt($d < 2 ? 'point PDP on track' : 'point PDP >2 m off track');
                cnt('point PDP within 2 m of a junction/end') if $dj < 2;
            }
            push @res, \%r;
        }
        my $broken = grep { (defined $_->{jd} && $_->{jd} >= 1) || (defined $_->{vd} && $_->{vd} >= 2) } @res;
        my $tag = $broken ? " [broken path]" : "";
        cnt("paths broken (a PDP off the TDB)") if $broken;
        for my $r (@res) { next unless defined $r->{v} && ($r->{fl} & 4);
            my $c = $between{join q(,), sort { $::a <=> $::b } @{$N{$r->{v}}{pins}}} // [];
            cnt("intermediate (flag 4) node: " . (@$c > 1 ? "on one of parallel tracks" : "on a single track") . $tag);
            cnt(sprintf("intermediate node at %d0%% of its track node", int($r->{frac} * 10))) if @$c > 1 && !$broken; }
        cnt("PDPs shared by several nodes$tag") if grep { $_ > 1 } values %pdpUse;
        cnt("$_$tag") for map { $_->{pk} } @res;
        # Walk the main chain.
        my %seen; my $k = 0; my @chain;
        while ($k != $NONE && $k < @res && !$seen{$k}++) { push @chain, $res[$k]; $k = $res[$k]{nx}; }
        cnt('nodes off the main chain', scalar(@res) - scalar(@chain));
        cnt('path ends at a TDB end node') if $chain[-1]{pdpf} == 2 && $N{$chain[-1]{j}}{type} eq 'end';
        cnt('path ends at a point') if $chain[-1]{pdpf} == 1;
        cnt('path starts at a point') if $chain[0]{pdpf} == 1;
        # Start and end flags, and how far they lie from a track end.
        for my $e (['start', $chain[0]], ['end', $chain[-1]]) {
            my ($what, $n) = @$e;
            my $p = $pdp[$pn[$n->{k}][3]];
            cnt(sprintf('%s PDP flags %d %d%s', $what, $p->[5], $p->[6], $tag));
            next unless defined $n->{v};
            my ($x, $z) = ($p->[0] * 2048 + $p->[2], $p->[1] * 2048 + $p->[4]);
            my $bd = 1e9; my $btype = '';
            for my $pin (@{$N{$n->{v}}{pins}}) { my $q = $N{$pin}{pos} or next;
                my $d = sqrt(($q->[0] - $x) ** 2 + ($q->[2] - $z) ** 2); ($bd, $btype) = ($d, $N{$pin}{type}) if $d < $bd; }
            my $bucket = $bd < 1 ? '<1 m' : $bd < 5 ? '1-5 m' : $bd < 50 ? '5-50 m' : '>50 m';
            cnt("$what point: nearest node of its track is a $btype, $bucket$tag");
        }
        # Junction node flags against the exit taken at facing junctions.
        my $trackBetween = sub { my ($u, $w) = @_;
            return $u->{v} if defined $u->{v} && !defined $w->{v};
            return $w->{v} if defined $w->{v} && !defined $u->{v};
            return $u->{v} if defined $u->{v};
            return ($between{join ',', sort { $::a <=> $::b } $u->{j}, $w->{j}} // [])->[0]; };
        for my $q (1 .. $#chain - 1) {
            my $n = $chain[$q]; next unless defined $n->{j} && $N{$n->{j}}{type} eq 'junction';
            my $tin = $trackBetween->($chain[$q - 1], $n); my $tout = $trackBetween->($n, $chain[$q + 1]);
            next unless defined $tin && defined $tout;
            my $p = $N{$n->{j}}{pins};
            my $facing = $p->[0] == $tin;
            my $kind = $facing ? (isMain($n->{j}, $tout) ? 'facing, main exit' : 'facing, OTHER exit') : 'trailing';
            cnt(sprintf('junction node flags %x: %s%s', $n->{fl} & 0xffff, $kind, $tag));
        }
        for my $q (0 .. $#chain - 1) {
            my ($ca, $cb) = ($chain[$q], $chain[$q + 1]);
            if (defined $ca->{j} && defined $cb->{j}) {
                if ($ca->{j} == $cb->{j}) { cnt(sprintf("pair J-J same junction, flags %x %x%s", $ca->{fl}, $cb->{fl}, $tag)); next; }
                my $c = $between{join ',', sort { $::a <=> $::b } $ca->{j}, $cb->{j}} // [];
                if (@$c > 1) {
                    # Tracks between the pair a train can use, given the nodes before and after:
                    # a train passes a junction between pin 0 and pin 1 or 2, never 1 to 2.
                    my $link = sub { my ($jn, $other) = @_; my $p = $N{$jn}{pins}; for my $i (0 .. $#$p) { return $i if $p->[$i] == $other; } return -1; };
                    my $through = sub { my ($jn, $in, $out) = @_; return 1 if !defined $in || !defined $out;
                        my ($x1, $y1) = ($link->($jn, $in), $link->($jn, $out)); return 1 if $x1 < 0 || $y1 < 0;
                        return (($x1 == 0) != ($y1 == 0)) ? 1 : 0; };
                    my $trackTo = sub { my ($n, $jn) = @_; return undef unless $n;
                        return defined $n->{j} ? ($between{join ',', sort { $::a <=> $::b } $n->{j}, $jn} // [])->[0] : $n->{v}; };
                    my $prevTrack = $q > 0 ? $trackTo->($chain[$q - 1], $ca->{j}) : undef;
                    my $nextTrack = $q + 2 <= $#chain ? $trackTo->($chain[$q + 2], $cb->{j}) : undef;
                    my $ok = grep { $through->($ca->{j}, $prevTrack, $_) && $through->($cb->{j}, $_, $nextTrack) } @$c;
                    cnt("pair J-J two tracks, usable by a train: $ok$tag");
                    printf "  usable %d %s nodes %d-%d\n", $ok, $name, $ca->{k}, $cb->{k} if $verbose && !$broken;
                }
                if (@$c == 1) { cnt('pair J-J adjacent'); }
                elsif (@$c > 1) { cnt("pair J-J adjacent, two tracks (ambiguous)$tag");
                    printf "  ambiguous %s nodes %d-%d\n", $name, $ca->{k}, $cb->{k} if $verbose; }
                else { cnt("pair J-J NOT adjacent (junctions skipped)$tag");
                    printf "  gap %s nodes %d-%d (%s %s)\n", $name, $ca->{k}, $cb->{k}, $ca->{j}, $cb->{j} if $verbose; }
            } elsif (defined $ca->{j}) {
                my $pins = $N{$cb->{v}}{pins};
                if (grep { $_ == $ca->{j} } @$pins) {
                    cnt('pair J-P adjacent');
                    my $c = $between{join ',', sort { $::a <=> $::b } @$pins} // [];
                    if (@$c > 1) {
                        cnt('pair J-P on one of two parallel tracks');
                        my ($m1, $m2) = map { isMain($_, $cb->{v}) } @$pins;
                        my $m = defined $m1 ? $m1 : $m2;
                        cnt('halfway node on the ' . (!defined $m ? 'track (no diverging junction?)' : $m ? 'MAIN route' : 'SIDING route') . $tag);
                    }
                } else { cnt('pair J-P NOT adjacent'); }
            } elsif (defined $cb->{j}) {
                my $pins = $N{$ca->{v}}{pins};
                cnt((grep { $_ == $cb->{j} } @$pins) ? 'pair P-J adjacent' : 'pair P-J NOT adjacent');
            } else {
                if ($ca->{v} == $cb->{v}) { cnt('pair P-P same track node'); }
                else {
                    my %pa = map { $_ => 1 } @{$N{$ca->{v}}{pins}};
                    cnt((grep { $pa{$_} } @{$N{$cb->{v}}{pins}}) ? 'pair P-P via one junction' : 'pair P-P NOT adjacent');
                    printf "  P-P %s nodes %d-%d\n", $name, $ca->{k}, $cb->{k} if $verbose;
                }
            }
        }
    }
}
for (sort keys %tot) { printf "%6d  %s\n", $tot{$_}, $_; }
