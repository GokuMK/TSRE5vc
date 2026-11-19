#pragma once
#include <QByteArray>
#include <QString>
#include <memory>
#include <vector>
#include <tsre/sim/SimulationFrame.h>

namespace Sim {

// The simulation on the wire (task server 05): frames, events and messages
// as bytes (little endian), and back. Pure: no socket, no Game but the
// client's EngLib (a vehicle's definition, by its file) and its run's input
// (the track's geometry, the cars' ends worked out from their places).
namespace Wire {

// The frames for every client, one frame after another (the server's): a
// frame whole, or what changed since the frame given before (the trains
// that changed, the shared parts' changed entries, the driven and heard
// trains' changed values). Each encoded once, whoever asks.
class FrameEncoder {
public:
    // The frame to send next; the one before it is what `changes` is
    // against.
    void next(std::shared_ptr<const SimulationFrame> frame);
    std::shared_ptr<const SimulationFrame> frame() const { return now; }
    // The frame whole; what changed since the frame before (whole when
    // there was none, or of another run).
    QByteArray whole();
    QByteArray changes();
    // The trains in the last `changes` (the measurements).
    int changedTrains() const { return changedCount; }

private:
    std::shared_ptr<const SimulationFrame> before;
    std::shared_ptr<const SimulationFrame> now;
    // Each train's state and ends as encoded (to find those that changed),
    // the frame before's and this one's.
    std::vector<QByteArray> trainsBefore, trainsNow;
    QByteArray wholeBytes, changesBytes;
    bool wholeMade = false, changesMade = false;
    int changedCount = 0;
    QByteArray encode(bool all);
};

// A client's side: frames made from what arrives, sharing with the frame
// before what did not change (as MakeFrame does on the host).
class FrameDecoder {
public:
    // The run's input this view made (its geometry places the cars).
    void setInput(std::shared_ptr<const SimulationInput> input);
    // The frame these bytes give, null when they do not follow on from
    // the frame it has, or are older (they are ignored; why in `error`).
    // `arrivalNs`: when they arrived (SteadyNs): the frame's publishedNs is
    // the server's stamp on this clock, by the smallest delay seen.
    std::shared_ptr<const SimulationFrame> decode(const QByteArray &bytes, long long arrivalNs,
                                                  QString *error = nullptr);
    std::shared_ptr<const SimulationFrame> frame() const { return last; }
    // Back to no frame (a new run, a reconnect: the next must be whole).
    void reset();
    // The longest age a remote frame's trains are moved on for (s): two
    // frame intervals and the spread of the delays seen.
    double drawnAgeMost() const;

private:
    std::shared_ptr<const SimulationInput> in;
    std::shared_ptr<const SimulationFrame> last;
    // The clocks' offset (arrival minus the server's stamp, the smallest
    // seen), and the spread above it lately.
    bool offsetKnown = false;
    long long offsetNs = 0;
    double spread = 0.0;
    long long lastServerNs = 0;
    double interval = 1.0 / 30.0;
};

// The run's events, and the messages for a client.
QByteArray EncodeEvents(const std::vector<TrainEvent> &events);
bool DecodeEvents(const QByteArray &bytes, std::vector<TrainEvent> &out);
QByteArray EncodeMessages(const std::vector<TrainMessage> &messages);
bool DecodeMessages(const QByteArray &bytes, std::vector<TrainMessage> &out);
QByteArray EncodeValue(const MessageValue &value);

}
}
