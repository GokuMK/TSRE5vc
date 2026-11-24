#ifndef TSRE_CAR_GANGWAYS_TEST_SUITE_H
#define TSRE_CAR_GANGWAYS_TEST_SUITE_H

namespace TsreTests {
// Walking between cars (task characters 05): EndPassage read and mirrored
// with the .sd's axes; when two facing ends open (Gangways::matches); which
// end faces which car, flipped or not; each car's own plate overlapping its
// neighbour's on straight track, a curve, cant on one car, slack and fitted
// carriers; a body walking across between two cars' worlds, standing and
// through a curve's start at 10 m/s, both ways, handed over once; a closed
// end stopping it; back inside from an end closing.
int runCarGangwaysSuite(bool verbose);
}

#endif
