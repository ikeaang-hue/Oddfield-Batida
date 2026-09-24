// Offline checks against the installed Batida AU, loaded the way a host loads it.
//
//   swiftc -O -o build/au_check tests/au_check.swift && build/au_check
//
// Run `killall -9 AudioComponentRegistrar` first after rebuilding the AU.
// Host values: knobs are normalised 0..1; menus (choice parameters) are indexes 0..N-1.

import AVFoundation
import AudioToolbox

setvbuf(stdout, nil, _IONBF, 0) // unbuffered, so output survives a crash

let sampleRate = 48000.0
let outDir = URL(fileURLWithPath: "build/au-renders", isDirectory: true)
var failures = 0

func check(_ ok: Bool, _ message: String) {
    print((ok ? "  ok    " : "  FAIL  ") + message)
    if !ok { failures += 1 }
}

func fourCC(_ s: String) -> OSType { s.utf8.reduce(0) { ($0 << 8) | OSType($1) } }

let description = AudioComponentDescription(componentType: kAudioUnitType_MusicDevice,
                                            componentSubType: fourCC("Btda"),
                                            componentManufacturer: fourCC("Ngsp"),
                                            componentFlags: 0, componentFlagsMask: 0)

func instantiate() -> AVAudioUnit {
    var result: AVAudioUnit?
    var failure: Error?
    AVAudioUnit.instantiate(with: description, options: []) { unit, error in
        result = unit
        failure = error
    }
    while result == nil && failure == nil { RunLoop.current.run(until: Date().addingTimeInterval(0.01)) }
    guard let unit = result else { fatalError("Could not load Batida AU: \(String(describing: failure))") }
    return unit
}

final class Renderer {
    let unit: AVAudioUnit
    let engine = AVAudioEngine()
    let format = AVAudioFormat(standardFormatWithSampleRate: sampleRate, channels: 2)!
    let buffer: AVAudioPCMBuffer

    init(_ unit: AVAudioUnit) {
        self.unit = unit
        buffer = AVAudioPCMBuffer(pcmFormat: format, frameCapacity: 512)!
        engine.attach(unit)
        engine.connect(unit, to: engine.mainMixerNode, format: format)
        try! engine.enableManualRenderingMode(.offline, format: format, maximumFrameCount: 512)
        try! engine.start()
    }

    func param(_ name: String) -> AUParameter {
        guard let p = unit.auAudioUnit.parameterTree?.allParameters.first(where: { $0.displayName == name })
        else { fatalError("No parameter named \(name)") }
        return p
    }

    func midi(_ bytes: [UInt8]) {
        bytes.withUnsafeBufferPointer { ptr in
            unit.auAudioUnit.scheduleMIDIEventBlock!(AUEventSampleTimeImmediate, 0, bytes.count, ptr.baseAddress!)
        }
    }

    // Plays one note (channel 1-16) and returns the left channel.
    func play(channel: Int, note: UInt8, velocity: UInt8 = 110, seconds: Double = 1.0, noteSeconds: Double = 0.2) -> [Float] {
        let ch = UInt8(channel - 1)
        var out: [Float] = []
        let total = Int(seconds * sampleRate)
        let offAt = Int(noteSeconds * sampleRate)
        midi([0x90 | ch, note, velocity])
        while out.count < total {
            if out.count <= offAt && out.count + 512 > offAt { midi([0x80 | ch, note, 0]) }
            _ = try! engine.renderOffline(512, to: buffer)
            let l = buffer.floatChannelData![0]
            out.append(contentsOf: UnsafeBufferPointer(start: l, count: Int(buffer.frameLength)))
        }
        return out
    }

    func silence(seconds: Double) { _ = play(channel: 16, note: 0, velocity: 1, seconds: seconds, noteSeconds: 0) }
}

// Drives the AU directly, like a host that feeds the side-chain: the input bus
// is pulled for audio on every render call. (AVAudioEngine can't connect
// anything into an instrument node.)
final class SidechainRenderer {
    let au: AUAudioUnit
    let format = AVAudioFormat(standardFormatWithSampleRate: sampleRate, channels: 2)!
    let out: AVAudioPCMBuffer
    var keyLevel: Float = 0
    var phase = 0.0
    var sampleTime = 0.0

    var params: [String: AUParameter] = [:]

    init(_ unit: AVAudioUnit) {
        au = unit.auAudioUnit
        out = AVAudioPCMBuffer(pcmFormat: format, frameCapacity: 512)!
        for p in au.parameterTree?.allParameters ?? [] { params[p.displayName] = p }
        try! au.outputBusses[0].setFormat(format)
        try! au.inputBusses[0].setFormat(format)
        au.inputBusses[0].isEnabled = true
        au.maximumFramesToRender = 512
        try! au.allocateRenderResources()
    }

    func param(_ name: String) -> AUParameter {
        guard let p = params[name] else { fatalError("no parameter \(name)") }
        return p
    }

    func midi(_ bytes: [UInt8]) {
        bytes.withUnsafeBufferPointer { au.scheduleMIDIEventBlock!(AUEventSampleTimeImmediate, 0, bytes.count, $0.baseAddress!) }
    }

    func play(note: UInt8, seconds: Double, noteSeconds: Double) -> [Float] {
        var result: [Float] = []
        let offAt = Int(noteSeconds * sampleRate)
        if note > 0 { midi([0x90, note, 110]) }
        while result.count < Int(seconds * sampleRate) {
            if note > 0 && result.count <= offAt && result.count + 512 > offAt { midi([0x80, note, 0]) }
            out.frameLength = 512
            var flags = AudioUnitRenderActionFlags()
            var ts = AudioTimeStamp()
            ts.mSampleTime = sampleTime
            ts.mFlags = .sampleTimeValid
            let level = keyLevel
            let status = au.renderBlock(&flags, &ts, 512, 0, out.mutableAudioBufferList) { _, _, frames, _, list in
                for b in UnsafeMutableAudioBufferListPointer(list) {
                    let p = b.mData!.assumingMemoryBound(to: Float.self)
                    var ph = self.phase
                    for i in 0..<Int(frames) {
                        p[i] = level * Float(sin(ph))
                        ph += 2 * Double.pi * 100 / sampleRate
                    }
                }
                self.phase += Double(frames) * 2 * Double.pi * 100 / sampleRate
                return noErr
            }
            precondition(status == noErr, "render failed: \(status)")
            sampleTime += 512
            result.append(contentsOf: UnsafeBufferPointer(start: out.floatChannelData![0], count: 512))
        }
        return result
    }
}

// Classic AU v2 host with host callbacks, the way Logic hands a plugin its
// tempo, position and play state. (The v3 bridge can't carry transport for a
// v2 instrument in this test setup.)
var v2Tempo = 120.0
var v2SampleTime = 0.0
var v2Playing = true

func renderWithHostTransport(runTransportMode: Bool, seconds: Double) -> [Float] {
    var desc = description
    var unit: AudioUnit?
    AudioComponentInstanceNew(AudioComponentFindNext(nil, &desc)!, &unit)
    let au = unit!
    var fmt = AudioStreamBasicDescription(mSampleRate: sampleRate, mFormatID: kAudioFormatLinearPCM,
        mFormatFlags: kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved,
        mBytesPerPacket: 4, mFramesPerPacket: 1, mBytesPerFrame: 4, mChannelsPerFrame: 2, mBitsPerChannel: 32, mReserved: 0)
    let size = UInt32(MemoryLayout<AudioStreamBasicDescription>.size)
    AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &fmt, size)
    var maxFrames: UInt32 = 512
    AudioUnitSetProperty(au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &maxFrames, 4)

    var callbacks = HostCallbackInfo()
    callbacks.beatAndTempoProc = { _, beat, tempo in
        beat?.pointee = v2SampleTime / sampleRate * v2Tempo / 60
        tempo?.pointee = v2Tempo
        return noErr
    }
    callbacks.musicalTimeLocationProc = { _, offset, numerator, denominator, downbeat in
        offset?.pointee = 0
        numerator?.pointee = 4
        denominator?.pointee = 4
        downbeat?.pointee = 0
        return noErr
    }
    callbacks.transportStateProc = { _, playing, changed, position, cycling, cycleStart, cycleEnd in
        playing?.pointee = DarwinBoolean(v2Playing)
        changed?.pointee = DarwinBoolean(false)
        position?.pointee = v2SampleTime
        cycling?.pointee = DarwinBoolean(false)
        return noErr
    }
    AudioUnitSetProperty(au, kAudioUnitProperty_HostCallbacks, kAudioUnitScope_Global, 0, &callbacks,
                         UInt32(MemoryLayout<HostCallbackInfo>.size))
    AudioUnitInitialize(au)

    // Builds start with every pattern empty: give pattern 1 its test beat.
    var classInfo: Unmanaged<CFPropertyList>?
    var infoSize = UInt32(MemoryLayout<Unmanaged<CFPropertyList>>.size)
    if AudioUnitGetProperty(au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &classInfo, &infoSize) == noErr,
       let dict = classInfo?.takeRetainedValue() as? [String: Any],
       let data = dict["jucePluginState"] as? Data {
        var withBeat = dict
        withBeat["jucePluginState"] = encodeState(withTestBeat(decodeState(data)))
        var plist: CFPropertyList = withBeat as CFDictionary
        AudioUnitSetProperty(au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &plist, infoSize)
    }

    if runTransportMode { // find "Run Mode" and set it to Transport (menus are indexes)
        var listSize: UInt32 = 0
        AudioUnitGetPropertyInfo(au, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, &listSize, nil)
        var ids = [AudioUnitParameterID](repeating: 0, count: Int(listSize) / 4)
        AudioUnitGetProperty(au, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, &ids, &listSize)
        for id in ids {
            var info = AudioUnitParameterInfo()
            var infoSize = UInt32(MemoryLayout<AudioUnitParameterInfo>.size)
            AudioUnitGetProperty(au, kAudioUnitProperty_ParameterInfo, kAudioUnitScope_Global, id, &info, &infoSize)
            if let name = info.cfNameString?.takeUnretainedValue() as String?, name == "Run Mode" {
                AudioUnitSetParameter(au, id, kAudioUnitScope_Global, 0, 1, 0)
            }
        }
    }

    let bufs = AudioBufferList.allocate(maximumBuffers: 2)
    let storage = (0..<2).map { _ in UnsafeMutablePointer<Float>.allocate(capacity: 512) }
    var ts = AudioTimeStamp()
    ts.mFlags = .sampleTimeValid
    var out: [Float] = []
    v2SampleTime = 0
    while out.count < Int(seconds * sampleRate) {
        for i in 0..<2 { bufs[i] = AudioBuffer(mNumberChannels: 1, mDataByteSize: 512 * 4, mData: storage[i]) }
        var flags = AudioUnitRenderActionFlags()
        ts.mSampleTime = v2SampleTime
        AudioUnitRender(au, &flags, &ts, 0, 512, bufs.unsafeMutablePointer)
        out.append(contentsOf: UnsafeBufferPointer(start: storage[0], count: 512))
        v2SampleTime += 512
    }
    AudioUnitUninitialize(au)
    AudioComponentInstanceDispose(au)
    return out
}

func peakDb(_ x: [Float]) -> Float {
    let p = x.map { abs($0) }.max() ?? 0
    return p > 0 ? 20 * log10(p) : -200
}

func zeroCrossingHz(_ x: [Float]) -> Double {
    var n = 0
    var last = 0
    for i in 1..<x.count where (x[i - 1] < 0) != (x[i] < 0) { n += 1; last = i }
    return last > 0 ? Double(n) * 0.5 * sampleRate / Double(last) : 0
}

func writeWav(_ x: [Float], _ name: String) {
    try? FileManager.default.createDirectory(at: outDir, withIntermediateDirectories: true)
    let fmt = AVAudioFormat(standardFormatWithSampleRate: sampleRate, channels: 1)!
    let buf = AVAudioPCMBuffer(pcmFormat: fmt, frameCapacity: AVAudioFrameCount(x.count))!
    buf.frameLength = AVAudioFrameCount(x.count)
    x.withUnsafeBufferPointer { buf.floatChannelData![0].update(from: $0.baseAddress!, count: x.count) }
    let file = try! AVAudioFile(forWriting: outDir.appendingPathComponent(name), settings: fmt.settings)
    try! file.write(from: buf)
}

// Pattern 1 as the checks expect it (a kick on steps 1, 3, 11 and 12, like
// the breakbeat's), unless the build already starts with the demo breakbeat.
func withTestBeat(_ xml: String) -> String {
    if xml.contains("<PATTERN index=\"0\"") { return xml }
    let beat = "<PATTERN index=\"0\" length=\"16\" xy=\"\"><TRACK voice=\"0\" length=\"16\" noteLength=\"0.5\" gate=\"1010000000110000\"/></PATTERN>"
    if xml.contains("<PATTERNS/>") { return xml.replacingOccurrences(of: "<PATTERNS/>", with: "<PATTERNS>" + beat + "</PATTERNS>") }
    return xml.replacingOccurrences(of: "</PATTERNS>", with: beat + "</PATTERNS>")
}

func giveTestBeat(_ r: Renderer) {
    guard let data = r.unit.auAudioUnit.fullState?["jucePluginState"] as? Data else { return }
    var state = r.unit.auAudioUnit.fullState!
    state["jucePluginState"] = encodeState(withTestBeat(decodeState(data)))
    r.unit.auAudioUnit.fullState = state
}

// JUCE state blob: magic, length, UTF-8 XML, 0.
func decodeState(_ data: Data) -> String {
    String(decoding: data.subdata(in: 8..<(data.count - 1)), as: UTF8.self)
}

func encodeState(_ xml: String) -> Data {
    var d = Data()
    var magic = UInt32(0x21324356).littleEndian
    let body = Data(xml.utf8)
    var length = UInt32(body.count + 8 + 1 - 9).littleEndian
    d.append(Data(bytes: &magic, count: 4))
    d.append(Data(bytes: &length, count: 4))
    d.append(body)
    d.append(0)
    return d
}

// ---------------------------------------------------------------------------

print("Batida AU offline check")

let names = ["Kick", "Rim", "Snare", "Clap", "Tom", "Bass", "ClosedHat", "OpenHat"]

print("Default kit through the drum map (notes 36-43, channel 1):")
do {
    let r = Renderer(instantiate())
    for v in 0..<8 {
        let x = r.play(channel: 1, note: UInt8(36 + v))
        let finite = x.allSatisfy { $0.isFinite }
        check(finite && peakDb(x) > -24 && peakDb(x) < 6,
              String(format: "voice %d %-9@ peak %5.1f dB", v + 1, names[v] as NSString, peakDb(x)))
        writeWav(x, "\(v + 1)-\(names[v]).wav")
    }
}

print("MIDI mode (host parameters):")
do {
    let r = Renderer(instantiate())
    giveTestBeat(r)
    r.silence(seconds: 0.2)
    check(peakDb(r.play(channel: 2, note: 64)) < -100, "drum map: channel 2 note 64 is silent")
    r.param("MIDI Mode").value = 1.0 // Chromatic
    r.param("Keys Sound").value = 1 // voice 2 (menus are indexes)
    r.silence(seconds: 0.1)
    let rim = r.play(channel: 2, note: 64)
    check(peakDb(rim) > -30, String(format: "chromatic: channel 2 note 64 plays the keys voice (%.1f dB)", peakDb(rim)))
    check(peakDb(r.play(channel: 11, note: 50)) > -30, "chromatic: any channel plays the keys voice")
    check(peakDb(r.play(channel: 10, note: 36)) > -30, "chromatic: channel 10 note 36 still plays the kick")
    check(peakDb(r.play(channel: 10, note: 50)) < -100, "chromatic: channel 10 outside the drum map and pattern keys is silent")
    check(peakDb(r.play(channel: 10, note: 60, seconds: 0.6, noteSeconds: 0.5)) > -40, "chromatic: channel 10 C3 plays pattern 1")
}

print("Chromatic play (keys voice 6 = bass, channel 1):")
do {
    let r = Renderer(instantiate())
    r.param("MIDI Mode").value = 1.0
    r.param("Keys Sound").value = 5
    let low = zeroCrossingHz(Array(r.play(channel: 1, note: 60, seconds: 0.6, noteSeconds: 0.5)[4800..<19200]))
    r.silence(seconds: 0.3)
    let high = zeroCrossingHz(Array(r.play(channel: 1, note: 72, seconds: 0.6, noteSeconds: 0.5)[4800..<19200]))
    check(abs(high / low - 2.0) < 0.1, String(format: "an octave up doubles the pitch (%.0f Hz → %.0f Hz)", low, high))
}

print("Sidechain input (kit compressor keyed from outside):")
do {
    let unit = instantiate()
    let bus = unit.auAudioUnit.inputBusses
    check(bus.count == 1, "the AU has one input bus (the sidechain): \(bus.count)")

    let r = SidechainRenderer(unit)
    r.param("Comp Detector").value = 1   // Sidechain (menu index)
    r.param("Comp Amount").value = 0.8
    r.param("XY Heat").value = 0         // compressor only, no pad offsets
    func bassLevel(_ key: Float) -> Float {
        r.keyLevel = key
        _ = r.play(note: 0, seconds: 0.5, noteSeconds: 0) // let the envelopes settle
        let x = r.play(note: 41, seconds: 0.6, noteSeconds: 0.6) // voice 6, bass (Gate)
        let tail = x[9600...]
        return 10 * log10(tail.map { $0 * $0 }.reduce(0, +) / Float(tail.count))
    }
    let open = bassLevel(0), keyed = bassLevel(1)
    check(keyed < open - 6, String(format: "a key signal ducks the chain (%.1f dB → %.1f dB)", open, keyed))
    check(abs(open - bassLevel(0)) < 1, "without a key the level comes back")
}

func energy(_ x: ArraySlice<Float>) -> Float { x.map { $0 * $0 }.reduce(0, +) }

print("Sequencer (pattern keys, Transport mode, saved patterns):")
do {
    // Pattern key through the real AU, host stopped: internal tempo.
    let r = Renderer(instantiate())
    giveTestBeat(r)
    let beat = r.play(channel: 1, note: 60, seconds: 2.0, noteSeconds: 1.5) // hold C3: pattern 1
    let playing = energy(beat[24000..<72000]), after = energy(beat[84000...])
    check(playing > 1 && after < playing * 0.01,
          String(format: "holding C3 plays the breakbeat, release stops it (%.1f vs %.4f)", playing, after))
    let silent = energy(r.play(channel: 1, note: 62, seconds: 1.0, noteSeconds: 0.9)[...])
    check(silent < 1e-6, "an empty pattern (D3 = pattern 3) is silent")

    // Transport mode follows the host tempo: the kick on step 3 (2 x 16th)
    // lands at 0.25 s at 120 bpm, at 0.5 s at 60 bpm.
    func kickAtQuarterSecond(_ bpm: Double) -> Float {
        v2Tempo = bpm
        v2Playing = true
        return energy(renderWithHostTransport(runTransportMode: true, seconds: 0.4)[12000..<13500])
    }
    let fast = kickAtQuarterSecond(120), slow = kickAtQuarterSecond(60)
    check(fast > slow * 20, String(format: "Transport mode follows the host tempo (%.2f vs %.4f at 0.25 s)", fast, slow))
    v2Playing = false
    let stopped = energy(renderWithHostTransport(runTransportMode: true, seconds: 0.4)[...])
    check(stopped < 1e-6, "Transport mode is silent while the host is stopped (and Play is off)")
    v2Playing = true
    let keysMode = energy(renderWithHostTransport(runTransportMode: false, seconds: 0.4)[...])
    check(keysMode < 1e-6, "Keys mode doesn't start by itself when the host plays")

    // Patterns are saved with the project: put a kick on every beat of pattern 2.
    guard let data = r.unit.auAudioUnit.fullState?["jucePluginState"] as? Data else { fatalError("no JUCE state") }
    var xml = decodeState(data)
    check(xml.contains("<PATTERNS"), "state XML contains the patterns")
    let kick = "<PATTERN index=\"1\" length=\"16\" xy=\"\"><TRACK voice=\"0\" length=\"16\" noteLength=\"0.5\" gate=\"1000100010001000\"/></PATTERN>"
    xml = xml.contains("<PATTERNS/>") ? xml.replacingOccurrences(of: "<PATTERNS/>", with: "<PATTERNS>" + kick + "</PATTERNS>")
                                     : xml.replacingOccurrences(of: "</PATTERNS>", with: kick + "</PATTERNS>")
    var state = r.unit.auAudioUnit.fullState!
    state["jucePluginState"] = encodeState(xml)
    let restored = Renderer(instantiate())
    restored.unit.auAudioUnit.fullState = state
    let p2 = restored.play(channel: 1, note: 61, seconds: 1.2, noteSeconds: 1.1) // C#3: pattern 2
    check(energy(p2[0..<3000]) > 0.1 && energy(p2[24000..<27000]) > 0.1, "a pattern written into a saved project plays after reload")
}

print("State round trip (parameters):")
do {
    let a = Renderer(instantiate())
    a.param("S1 FM Pitch").value = 0.75
    a.param("S1 Drive").value = 0.8
    a.param("S1 Drive Type").value = 1.0
    a.param("XY Character").value = 0.9
    a.param("XY Heat").value = 0.7
    a.param("S3 Chain").value = 0.25
    a.param("EQ Follow XY").value = 0
    let original = a.play(channel: 1, note: 36)
    let state = a.unit.auAudioUnit.fullState

    let b = Renderer(instantiate())
    b.unit.auAudioUnit.fullState = state
    check(abs(b.param("S1 FM Pitch").value - 0.75) < 1e-4, "restored S1 FM Pitch = \(b.param("S1 FM Pitch").value)")
    check(abs(b.param("XY Heat").value - 0.7) < 1e-4 && abs(b.param("S3 Chain").value - 0.25) < 1e-4
              && b.param("EQ Follow XY").value == 0, "restored pad position, Chain amount and Follow XY")
    let restored = b.play(channel: 1, note: 36)
    let diff = zip(original, restored).map { abs($0 - $1) }.max() ?? 1
    check(diff < 1e-4, String(format: "restored instance renders the same audio (max diff %.2e)", diff))
}

print("State round trip (sample path):")
do {
    // A 0.3 s, 440 Hz test tone.
    let tone = (0..<Int(0.3 * sampleRate)).map { Float(0.5 * sin(2 * Double.pi * 440 * Double($0) / sampleRate)) }
    writeWav(tone, "test-tone.wav")
    let tonePath = outDir.appendingPathComponent("test-tone.wav").standardizedFileURL.path

    let base = Renderer(instantiate())
    guard let data = base.unit.auAudioUnit.fullState?["jucePluginState"] as? Data else { fatalError("no JUCE state") }
    var xml = decodeState(data)
    check(xml.contains("id=\"v1_src_mode\""), "state XML contains the parameter tree")

    // Switch voice 1 to Sample and point it at the tone.
    xml = xml.replacingOccurrences(of: #"<PARAM id="v1_src_mode" value="[^"]*"/>"#,
                                   with: #"<PARAM id="v1_src_mode" value="1.0"/>"#, options: .regularExpression)
    // Dry, so the pitch check hears the sample itself rather than the chain.
    xml = xml.replacingOccurrences(of: #"<PARAM id="v1_chain_amt" value="[^"]*"/>"#,
                                   with: #"<PARAM id="v1_chain_amt" value="0.0"/>"#, options: .regularExpression)
    check(xml.contains("<SAMPLES/>"), "state XML has an (empty) sample list")
    xml = xml.replacingOccurrences(of: "<SAMPLES/>",
                                   with: "<SAMPLES><SAMPLE voice=\"0\" path=\"\(tonePath)\"/></SAMPLES>")

    var state = base.unit.auAudioUnit.fullState!
    state["jucePluginState"] = encodeState(xml)
    let r = Renderer(instantiate())
    r.unit.auAudioUnit.fullState = state
    r.silence(seconds: 0.1)
    let x = r.play(channel: 1, note: 36)
    // 100-175 ms: voice 1's pitch sweep has settled and the tone is still playing
    // (the sweep plays its start fast, so it ends around 200 ms).
    let hz = zeroCrossingHz(Array(x[4800..<8400]))
    check(peakDb(x) > -26 && abs(hz - 440) < 10, String(format: "voice 1 plays the restored sample (%.0f Hz, %.1f dB)", hz, peakDb(x)))

    let missing = xml.replacingOccurrences(of: tonePath, with: "/nonexistent/gone.wav")
    state["jucePluginState"] = encodeState(missing)
    let m = Renderer(instantiate())
    m.unit.auAudioUnit.fullState = state
    let silent = m.play(channel: 1, note: 36)
    check(peakDb(silent) < -100, "a missing sample leaves the voice silent (no crash)")
    let saved = decodeState(m.unit.auAudioUnit.fullState!["jucePluginState"] as! Data)
    check(saved.contains("/nonexistent/gone.wav"), "the missing path is kept when the project is saved again")
}

print(failures == 0 ? "\nPASS" : "\nFAIL: \(failures) check(s)")
print("Renders in \(outDir.path)")
exit(failures == 0 ? 0 : 1)
