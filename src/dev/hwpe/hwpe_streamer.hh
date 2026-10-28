#ifndef __DEV_HWPE_HWPE_STREAMER_HH__
#define __DEV_HWPE_HWPE_STREAMER_HH__

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "base/statistics.hh"
#include "base/types.hh"
#include "mem/packet.hh"
#include "sim/eventq.hh"
#include "sim/serialize.hh"

namespace gem5
{

class HWPEBase;
class Streamer;

// Configuration of an address generator, with the fields of hwpe-stream's
// ctrl_addressgen_v3_t. dims is 1, 2 or 3: the RTL's dim_enable_1h of 2'b00,
// 2'b01 and 2'b11.
struct AddressGenConfig
{
    Addr base = 0;            // address of the first access
    uint32_t totLen = 0;      // number of accesses
    int32_t d0Stride = 0;     // bytes from one access to the next
    // Only for 2D and 3D (dims 2 and 3): after d0Len accesses, the next
    // line starts d1Stride bytes after the start of the previous line;
    // after d1Len lines (3D only), the next block starts d2Stride bytes
    // after the start of the previous block. totLen ends the walk.
    uint32_t d0Len = 0;
    int32_t d1Stride = 0;
    uint32_t d1Len = 0;
    int32_t d2Stride = 0;
    unsigned dims = 1;
};

// Generates the addresses of a stream's accesses, one per call of next()
// (hwpe_stream_addressgen_v3).
class AddressGen : public Serializable
{
  public:
    void start(const AddressGenConfig &config);
    // All totLen addresses have been generated
    bool done() const { return count == config.totLen; }
    // Returns the next address and advances
    Addr next();
    // The next address is the stream's last
    bool atLast() const { return count + 1 == config.totLen; }

    void serialize(CheckpointOut &cp) const override;
    void unserialize(CheckpointIn &cp) override;

  private:
    AddressGenConfig config;
    uint32_t count = 0;
};

// Common base of Source and Sink. A stream unit moves one stream of data
// between the TCDM and the engine (the subclass's computation). It has an
// address generator and a buffer of up to 'depth' beats, where a beat is
// the data of one TCDM access.
class StreamUnit : public statistics::Group, public Serializable
{
  public:
    StreamUnit(Streamer &streamer, const std::string &name, unsigned depth);
    virtual ~StreamUnit() = default;

    const std::string &name() const { return _name; }

  protected:
    friend class Streamer;

    // The Streamer's interface to the unit.
    // The unit wants to make a TCDM access now.
    virtual bool wantsAccess() const = 0;
    // Builds that access. The unit attaches a sequence number of its own
    // choice with tag(); the Streamer passes it back to recvResp() along
    // with the response.
    virtual PacketPtr makeAccess() = 0;
    virtual void recvResp(PacketPtr pkt, uint64_t seq) = 0;
    void tag(PacketPtr pkt, uint64_t seq);
    // Drops the current job: the buffered beats, and the accesses still
    // in flight (their responses are not passed to the unit)
    virtual void clear() = 0;

    Streamer &streamer;
    const std::string _name;
    const unsigned depth;
    AddressGen gen;

    statistics::Scalar accesses;
};

// Reads a stream from the TCDM for the engine (TCDM -> engine). Reads the
// beats in address order and hands them to the engine in that order, even
// if the responses come back out of order. At most 'depth' beats are
// buffered, counting the reads still in flight.
class Source : public StreamUnit
{
  public:
    using StreamUnit::StreamUnit;

    // Starts a job: config.totLen beats of beat_bytes each. The last beat
    // has last_beat_bytes instead (0 means beat_bytes), so that a stream
    // that ends in the middle of a beat does not read past its end.
    void start(const AddressGenConfig &config, unsigned beat_bytes,
               unsigned last_beat_bytes = 0);
    // The engine's interface: ready() says the oldest beat has arrived,
    // front() returns its data, pop() removes it.
    bool ready() const { return !beats.empty() && beats.front().valid; }
    const std::vector<uint8_t> &front() const;
    void pop();
    // All beats have been read and taken by the engine
    bool exhausted() const { return gen.done() && beats.empty(); }

    void serialize(CheckpointOut &cp) const override;
    void unserialize(CheckpointIn &cp) override;

  protected:
    bool wantsAccess() const override;
    PacketPtr makeAccess() override;
    void recvResp(PacketPtr pkt, uint64_t seq) override;
    void clear() override;

  private:
    struct Beat
    {
        std::vector<uint8_t> data;
        bool valid = false;   // the read's response has arrived
    };

    unsigned beatBytes = 0;
    unsigned lastBeatBytes = 0;
    // Beats read or being read, oldest first. The sequence number of a
    // beat is its position in the stream, so beats[0] has number 'popped'.
    std::deque<Beat> beats;
    uint64_t popped = 0;      // beats the engine has taken so far
};

// Writes a stream from the engine to the TCDM (engine -> TCDM). Writes the
// beats the engine pushes, in order, to the addresses of its generator.
// At most 'depth' beats are buffered.
class Sink : public StreamUnit
{
  public:
    using StreamUnit::StreamUnit;

    // Starts a job: config.totLen beats of at most beat_bytes each.
    // TODO: an HWPE whose output length depends on the data (FAST's
    // corners, if one job wrote a beat per corner) needs a finish() that
    // ends the job before totLen.
    void start(const AddressGenConfig &config, unsigned beat_bytes);
    // The engine's interface: room() says push() may be called. push()
    // hands over a beat of 'bytes' bytes, written from the beat's start
    // address (so the valid bytes must be contiguous from the start).
    // pushMasked() hands over a beat with a byte enable for each of its
    // bytes (the RTL's strb): only the enabled bytes are written. With no
    // byte enabled the access is still made and writes nothing, as in the
    // RTL.
    bool room() const { return beats.size() < depth; }
    void push(const uint8_t *data, unsigned bytes);
    void pushMasked(const uint8_t *data, const std::vector<bool> &enables);
    // All beats have been written and acknowledged
    bool done() const;

    void serialize(CheckpointOut &cp) const override;
    void unserialize(CheckpointIn &cp) override;

  protected:
    bool wantsAccess() const override { return !beats.empty(); }
    PacketPtr makeAccess() override;
    void recvResp(PacketPtr pkt, uint64_t seq) override;
    void clear() override;

  private:
    struct Beat
    {
        std::vector<uint8_t> data;
        // One flag (0 or 1) per byte of data for a masked write; empty
        // for an unmasked one
        std::vector<uint8_t> enables;
    };

    unsigned beatBytes = 0;
    std::deque<Beat> beats;     // pushed, not yet sent
    unsigned inFlight = 0;      // sent, not yet acknowledged
};

// The HWPE's streamer (like hci_core_mux_dynamic): all stream units share
// the HWPE's one TCDM port. It sends at most one access per cycle, chosen
// round robin among the units that want one. If the TCDM refuses an
// access, nothing else is sent until the TCDM has accepted it.
class Streamer : public statistics::Group, public Serializable
{
  public:
    Streamer(HWPEBase &hwpe);

    // Only called in the HWPE subclass's constructor.
    Source &addSource(const std::string &name, unsigned depth);
    Sink &addSink(const std::string &name, unsigned depth);

    // A unit may want to make an access: run tick() at the next clock edge
    // (or one cycle later if an access was already sent in this cycle).
    void wake();

    void serialize(CheckpointOut &cp) const override;
    void unserialize(CheckpointIn &cp) override;

  private:
    friend class HWPEBase;
    friend class StreamUnit;
    friend class Source;
    friend class Sink;

    // Attached to every packet a unit sends: the unit, and the sequence
    // number the unit chose (see StreamUnit::tag())
    struct StreamState : public Packet::SenderState
    {
        StreamUnit *unit;
        uint64_t seq;
        StreamState(StreamUnit *_unit, uint64_t _seq)
            : unit(_unit), seq(_seq)
        {}
    };

    // Called by HWPEBase: a response arrived, a refused access was
    // accepted, a packet of a cleared job, a soft clear
    void recvResp(PacketPtr pkt);
    void retried();
    void dropPacket(PacketPtr pkt);
    void clear();

    // Called by the units when data arrived or buffer space was freed;
    // passes it on to the HWPE subclass (streamProgress())
    void progress();
    // A request for an access by this HWPE
    RequestPtr request(Addr addr, unsigned size) const;

    // Sends at most one access (see the class comment)
    void tick();
    // The start of the current clock cycle: curTick() if it is on a clock
    // edge, else the last edge before it
    Tick cycleStart() const;

    HWPEBase &hwpe;
    std::vector<std::unique_ptr<StreamUnit>> units;
    unsigned nextUnit = 0;        // unit the round robin checks first
    Tick lastSend = MaxTick;      // start of the cycle of the last access
    EventFunctionWrapper tickEvent;

    statistics::Scalar refusals;
    statistics::Scalar conflicts;
};

} // namespace gem5

#endif // __DEV_HWPE_HWPE_STREAMER_HH__
