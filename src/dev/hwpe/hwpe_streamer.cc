#include "dev/hwpe/hwpe_streamer.hh"

#include <cstring>

#include "base/bitfield.hh"
#include "base/cast.hh"
#include "base/intmath.hh"
#include "base/logging.hh"
#include "base/trace.hh"
#include "debug/HWPE.hh"
#include "dev/hwpe/hwpe_base.hh"
#include "mem/request.hh"

namespace gem5
{

// ---- address generator ------------------------------------------------

void
AddressGen::start(const AddressGenConfig &_config)
{
    fatal_if(_config.dims < 1 || _config.dims > 3,
             "HWPE: %d-dimensional address generation; 1 to 3",
             _config.dims);
    fatal_if(_config.dims >= 2 && _config.d0Len == 0,
             "HWPE: %dD address generation with d0Len 0", _config.dims);
    fatal_if(_config.dims == 3 && _config.d1Len == 0,
             "HWPE: 3D address generation with d1Len 0");
    config = _config;
    count = 0;
}

Addr
AddressGen::next()
{
    panic_if(done(), "HWPE: address generator stepped past its end");
    // The position of access 'count' in its line, its line in its block,
    // and its block (see AddressGenConfig)
    uint64_t pos = count, line = 0, block = 0;
    if (config.dims >= 2) {
        pos = count % config.d0Len;
        line = count / config.d0Len;
    }
    if (config.dims == 3) {
        block = line / config.d1Len;
        line %= config.d1Len;
    }
    // Addresses wrap at 32 bits, as in the RTL
    const Addr addr = (config.base + int64_t(pos) * config.d0Stride +
                       int64_t(line) * config.d1Stride +
                       int64_t(block) * config.d2Stride) & mask(32);
    ++count;
    return addr;
}

void
AddressGen::serialize(CheckpointOut &cp) const
{
    paramOut(cp, "base", config.base);
    paramOut(cp, "totLen", config.totLen);
    paramOut(cp, "d0Stride", config.d0Stride);
    paramOut(cp, "d0Len", config.d0Len);
    paramOut(cp, "d1Stride", config.d1Stride);
    paramOut(cp, "d1Len", config.d1Len);
    paramOut(cp, "d2Stride", config.d2Stride);
    paramOut(cp, "dims", config.dims);
    SERIALIZE_SCALAR(count);
}

void
AddressGen::unserialize(CheckpointIn &cp)
{
    paramIn(cp, "base", config.base);
    paramIn(cp, "totLen", config.totLen);
    paramIn(cp, "d0Stride", config.d0Stride);
    paramIn(cp, "d0Len", config.d0Len);
    paramIn(cp, "d1Stride", config.d1Stride);
    paramIn(cp, "d1Len", config.d1Len);
    paramIn(cp, "d2Stride", config.d2Stride);
    paramIn(cp, "dims", config.dims);
    UNSERIALIZE_SCALAR(count);
}

// ---- stream units -----------------------------------------------------

StreamUnit::StreamUnit(Streamer &_streamer, const std::string &name,
                       unsigned _depth)
    : statistics::Group(&_streamer, name.c_str()),
      streamer(_streamer),
      _name(name),
      depth(_depth),
      ADD_STAT(accesses, statistics::units::Count::get(),
               "TCDM accesses made")
{
    fatal_if(depth < 1, "HWPE: stream %s has no buffer", name);
}

void
StreamUnit::tag(PacketPtr pkt, uint64_t seq)
{
    pkt->pushSenderState(new Streamer::StreamState(this, seq));
}

void
Source::start(const AddressGenConfig &config, unsigned beat_bytes,
              unsigned last_beat_bytes)
{
    panic_if(!beats.empty(), "HWPE: source %s started while busy", name());
    panic_if(last_beat_bytes > beat_bytes,
             "HWPE: source %s's last beat is longer than a beat", name());
    gen.start(config);
    beatBytes = beat_bytes;
    lastBeatBytes = last_beat_bytes ? last_beat_bytes : beat_bytes;
    popped = 0;
    streamer.wake();
}

bool
Source::wantsAccess() const
{
    return !gen.done() && beats.size() < depth;
}

PacketPtr
Source::makeAccess()
{
    const unsigned bytes = gen.atLast() ? lastBeatBytes : beatBytes;
    const Addr addr = gen.next();
    const uint64_t seq = popped + beats.size();
    beats.push_back(Beat{std::vector<uint8_t>(beatBytes, 0), false});
    PacketPtr pkt = new Packet(streamer.request(addr, bytes),
                               MemCmd::ReadReq);
    pkt->allocate();
    tag(pkt, seq);
    ++accesses;
    DPRINTF(HWPE, "%s: read %d bytes at %#x\n", name(), bytes, addr);
    return pkt;
}

void
Source::recvResp(PacketPtr pkt, uint64_t seq)
{
    Beat &beat = beats.at(seq - popped);
    std::memcpy(beat.data.data(), pkt->getConstPtr<uint8_t>(),
                pkt->getSize());
    beat.valid = true;
    delete pkt;
    streamer.progress();
}

const std::vector<uint8_t> &
Source::front() const
{
    panic_if(!ready(), "HWPE: source %s has no beat", name());
    return beats.front().data;
}

void
Source::pop()
{
    panic_if(!ready(), "HWPE: source %s has no beat", name());
    beats.pop_front();
    ++popped;
    // The freed buffer slot allows another read
    streamer.wake();
}

void
Source::clear()
{
    beats.clear();
    popped = 0;
    gen.start(AddressGenConfig());
}

// Each unit writes its own values before its "gen" subsection: values
// written after a subsection end up inside that subsection.
void
Source::serialize(CheckpointOut &cp) const
{
    SERIALIZE_SCALAR(beatBytes);
    SERIALIZE_SCALAR(lastBeatBytes);
    SERIALIZE_SCALAR(popped);
    // Checkpoints are taken drained, so every read has returned
    const unsigned num_beats = beats.size();
    SERIALIZE_SCALAR(num_beats);
    for (unsigned n = 0; n < num_beats; ++n) {
        panic_if(!beats[n].valid, "HWPE: source %s checkpointed with a "
                 "read outstanding", name());
        arrayParamOut(cp, csprintf("beat%d", n), beats[n].data);
    }
    gen.serializeSection(cp, "gen");
}

void
Source::unserialize(CheckpointIn &cp)
{
    gen.unserializeSection(cp, "gen");
    UNSERIALIZE_SCALAR(beatBytes);
    UNSERIALIZE_SCALAR(lastBeatBytes);
    UNSERIALIZE_SCALAR(popped);
    unsigned num_beats;
    UNSERIALIZE_SCALAR(num_beats);
    beats.clear();
    for (unsigned n = 0; n < num_beats; ++n) {
        Beat beat;
        arrayParamIn(cp, csprintf("beat%d", n), beat.data);
        beat.valid = true;
        beats.push_back(std::move(beat));
    }
}

void
Sink::start(const AddressGenConfig &config, unsigned beat_bytes)
{
    panic_if(!beats.empty() || inFlight,
             "HWPE: sink %s started while busy", name());
    gen.start(config);
    beatBytes = beat_bytes;
}

void
Sink::push(const uint8_t *data, unsigned bytes)
{
    panic_if(!room(), "HWPE: sink %s is full", name());
    panic_if(bytes > beatBytes, "HWPE: a %d-byte beat for sink %s of %d",
             bytes, name(), beatBytes);
    beats.push_back(Beat{std::vector<uint8_t>(data, data + bytes), {}});
    streamer.wake();
}

void
Sink::pushMasked(const uint8_t *data, const std::vector<bool> &enables)
{
    panic_if(!room(), "HWPE: sink %s is full", name());
    panic_if(enables.size() > beatBytes,
             "HWPE: a %d-byte beat for sink %s of %d", enables.size(),
             name(), beatBytes);
    beats.push_back(Beat{std::vector<uint8_t>(data, data + enables.size()),
                         std::vector<uint8_t>(enables.begin(),
                                              enables.end())});
    streamer.wake();
}

bool
Sink::done() const
{
    return gen.done() && beats.empty() && inFlight == 0;
}

PacketPtr
Sink::makeAccess()
{
    panic_if(gen.done(), "HWPE: sink %s got more beats than its job's",
             name());
    const Addr addr = gen.next();
    const Beat &beat = beats.front();
    RequestPtr req = streamer.request(addr, beat.data.size());
    PacketPtr pkt = new Packet(req, MemCmd::WriteReq);
    pkt->allocate();
    pkt->setData(beat.data.data());
    // After setData(), which does not take a masked write
    if (!beat.enables.empty())
        req->setByteEnable(std::vector<bool>(beat.enables.begin(),
                                             beat.enables.end()));
    tag(pkt, 0);
    DPRINTF(HWPE, "%s: write %d bytes at %#x%s\n", name(), beat.data.size(),
            addr, beat.enables.empty() ? "" : " (masked)");
    beats.pop_front();
    ++inFlight;
    ++accesses;
    // The beat left the buffer: the engine may push another
    streamer.progress();
    return pkt;
}

void
Sink::recvResp(PacketPtr pkt, uint64_t seq)
{
    --inFlight;
    delete pkt;
    streamer.progress();
}

void
Sink::clear()
{
    beats.clear();
    inFlight = 0;
    gen.start(AddressGenConfig());
}

void
Sink::serialize(CheckpointOut &cp) const
{
    panic_if(inFlight, "HWPE: sink %s checkpointed with a write outstanding",
             name());
    SERIALIZE_SCALAR(beatBytes);
    const unsigned num_beats = beats.size();
    SERIALIZE_SCALAR(num_beats);
    for (unsigned n = 0; n < num_beats; ++n) {
        arrayParamOut(cp, csprintf("beat%d", n), beats[n].data);
        arrayParamOut(cp, csprintf("beat%d.enables", n), beats[n].enables);
    }
    gen.serializeSection(cp, "gen");
}

void
Sink::unserialize(CheckpointIn &cp)
{
    gen.unserializeSection(cp, "gen");
    UNSERIALIZE_SCALAR(beatBytes);
    unsigned num_beats;
    UNSERIALIZE_SCALAR(num_beats);
    beats.clear();
    for (unsigned n = 0; n < num_beats; ++n) {
        Beat beat;
        arrayParamIn(cp, csprintf("beat%d", n), beat.data);
        arrayParamIn(cp, csprintf("beat%d.enables", n), beat.enables);
        beats.push_back(std::move(beat));
    }
    inFlight = 0;
}

// ---- streamer ---------------------------------------------------------

Streamer::Streamer(HWPEBase &_hwpe, unsigned mux_channels)
    : statistics::Group(&_hwpe, "streamer"),
      hwpe(_hwpe),
      muxChannels(mux_channels),
      tickEvent([this] { tick(); }, _hwpe.name() + ".streamer.tick"),
      ADD_STAT(refusals, statistics::units::Count::get(),
               "Accesses the TCDM refused at first"),
      ADD_STAT(conflicts, statistics::units::Count::get(),
               "Accesses that waited a cycle for another unit's")
{
    // The RTL's counter is $clog2(NB_IN_CHAN) bits wide, so it only wraps
    // at NB_IN_CHAN if that is a power of two
    fatal_if(!isPowerOf2(muxChannels), "%s: %d mux channels; a power of two",
             hwpe.name(), muxChannels);
}

Source &
Streamer::addSource(const std::string &name, unsigned depth)
{
    fatal_if(units.size() >= muxChannels, "%s: more streams than the %d mux "
             "channels", hwpe.name(), muxChannels);
    auto *source = new Source(*this, name, depth);
    units.emplace_back(source);
    return *source;
}

Sink &
Streamer::addSink(const std::string &name, unsigned depth)
{
    fatal_if(units.size() >= muxChannels, "%s: more streams than the %d mux "
             "channels", hwpe.name(), muxChannels);
    auto *sink = new Sink(*this, name, depth);
    units.emplace_back(sink);
    return *sink;
}

RequestPtr
Streamer::request(Addr addr, unsigned size) const
{
    return std::make_shared<Request>(addr, size, 0, hwpe.requestorId);
}

void
Streamer::wake()
{
    if (units.empty())
        return;
    // One access per cycle: if one was already sent in this cycle, wait
    // for the next
    Tick when = hwpe.clockEdge();
    if (when == lastSend)
        when = hwpe.clockEdge(Cycles(1));
    if (!tickEvent.scheduled())
        hwpe.schedule(tickEvent, when);
    else if (when < tickEvent.when())
        hwpe.reschedule(tickEvent, when);
}

Tick
Streamer::cycleStart() const
{
    const Tick edge = hwpe.clockEdge();
    return edge == curTick() ? edge : edge - hwpe.clockPeriod();
}

void
Streamer::progress()
{
    hwpe.streamProgress();
}

void
Streamer::tick()
{
    // Send nothing while a refused access waits to be resent (retried()
    // wakes us again)
    if (!hwpe.canSendTcdm())
        return;
    // hci_core_mux_dynamic: of the units that want an access, the last in
    // the order rrCounter, rrCounter + 1, ... wins; the counter steps once
    // per access, whichever unit made it, and only a soft clear resets it.
    // Count the others that want one too (conflicts)
    StreamUnit *chosen = nullptr;
    unsigned waiting = 0;
    for (unsigned n = muxChannels; n-- > 0;) {
        const unsigned index = (rrCounter + n) % muxChannels;
        if (index >= units.size() || !units[index]->wantsAccess())
            continue;
        if (!chosen)
            chosen = units[index].get();
        else
            ++waiting;
    }
    if (!chosen)
        return;
    rrCounter = (rrCounter + 1) % muxChannels;
    conflicts += waiting;
    lastSend = cycleStart();
    PacketPtr pkt = chosen->makeAccess();
    hwpe.sendTcdm(pkt);
    if (hwpe.tcdmPort.waiting()) {
        ++refusals;
        return;
    }
    // If a unit still wants an access, tick again in the next cycle
    for (const auto &unit : units) {
        if (unit->wantsAccess()) {
            wake();
            break;
        }
    }
}

void
Streamer::recvResp(PacketPtr pkt)
{
    auto *state = safe_cast<StreamState *>(pkt->popSenderState());
    StreamUnit *unit = state->unit;
    const uint64_t seq = state->seq;
    delete state;
    unit->recvResp(pkt, seq);
}

// The TCDM has now accepted the access it refused: that access counts as
// sent in this cycle.
void
Streamer::retried()
{
    lastSend = cycleStart();
    wake();
}

// A packet of a job that a soft clear dropped: free it without telling the
// unit.
void
Streamer::dropPacket(PacketPtr pkt)
{
    delete pkt->popSenderState();
    delete pkt;
}

void
Streamer::clear()
{
    for (auto &unit : units)
        unit->clear();
    if (tickEvent.scheduled())
        hwpe.deschedule(tickEvent);
    rrCounter = 0;
}

void
Streamer::serialize(CheckpointOut &cp) const
{
    SERIALIZE_SCALAR(rrCounter);
    SERIALIZE_SCALAR(lastSend);
    const Tick tick_tick =
        tickEvent.scheduled() ? tickEvent.when() : MaxTick;
    SERIALIZE_SCALAR(tick_tick);
    for (const auto &unit : units)
        unit->serializeSection(cp, unit->name());
}

void
Streamer::unserialize(CheckpointIn &cp)
{
    UNSERIALIZE_SCALAR(rrCounter);
    UNSERIALIZE_SCALAR(lastSend);
    Tick tick_tick;
    UNSERIALIZE_SCALAR(tick_tick);
    if (tick_tick != MaxTick)
        hwpe.schedule(tickEvent, tick_tick);
    for (auto &unit : units)
        unit->unserializeSection(cp, unit->name());
}

} // namespace gem5
