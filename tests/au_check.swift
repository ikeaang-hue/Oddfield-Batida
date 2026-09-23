// Offline checks against the installed Batida AU, loaded the way a host loads it.
//
//   swiftc -O -o build/au_check tests/au_check.swift && build/au_check
//
// Run `killall -9 AudioComponentRegistrar` first after rebuilding the AU.
// Host parameters are normalised 0..1 (JUCE AU), not in real units.

import AVFoundation
import AudioToolbox

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

let names = ["Kick", "Rim", "Snare", "Tom", "Zap", "Bass", "ClosedHat", "OpenHat"]

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

print("MIDI mode (host parameter, normalised):")
do {
    let r = Renderer(instantiate())
    r.silence(seconds: 0.2)
    check(peakDb(r.play(channel: 2, note: 64)) < -100, "drum map: channel 2 note 64 is silent")
    r.param("MIDI Mode").value = 1.0 // Split
    r.silence(seconds: 0.1)
    let rim = r.play(channel: 2, note: 64)
    check(peakDb(rim) > -30, String(format: "split: channel 2 note 64 plays voice 2 (%.1f dB)", peakDb(rim)))
    check(peakDb(r.play(channel: 10, note: 36)) > -30, "split: channel 10 note 36 plays the kick")
    check(peakDb(r.play(channel: 11, note: 60)) < -100, "split: channel 11 is ignored")
}

print("Chromatic play (split mode, voice 6 bass on channel 6):")
do {
    let r = Renderer(instantiate())
    r.param("MIDI Mode").value = 1.0
    let low = zeroCrossingHz(Array(r.play(channel: 6, note: 60, seconds: 0.6, noteSeconds: 0.5)[4800..<19200]))
    r.silence(seconds: 0.3)
    let high = zeroCrossingHz(Array(r.play(channel: 6, note: 72, seconds: 0.6, noteSeconds: 0.5)[4800..<19200]))
    check(abs(high / low - 2.0) < 0.1, String(format: "an octave up doubles the pitch (%.0f Hz → %.0f Hz)", low, high))
}

print("State round trip (parameters):")
do {
    let a = Renderer(instantiate())
    a.param("V1 FM Pitch").value = 0.75
    a.param("V1 Drive").value = 0.8
    a.param("V1 Drive Type").value = 1.0
    let original = a.play(channel: 1, note: 36)
    let state = a.unit.auAudioUnit.fullState

    let b = Renderer(instantiate())
    b.unit.auAudioUnit.fullState = state
    check(abs(b.param("V1 FM Pitch").value - 0.75) < 1e-4, "restored V1 FM Pitch = \(b.param("V1 FM Pitch").value)")
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
    check(xml.contains("<SAMPLES/>"), "state XML has an (empty) sample list")
    xml = xml.replacingOccurrences(of: "<SAMPLES/>",
                                   with: "<SAMPLES><SAMPLE voice=\"0\" path=\"\(tonePath)\"/></SAMPLES>")

    var state = base.unit.auAudioUnit.fullState!
    state["jucePluginState"] = encodeState(xml)
    let r = Renderer(instantiate())
    r.unit.auAudioUnit.fullState = state
    r.silence(seconds: 0.1)
    let x = r.play(channel: 1, note: 36)
    let hz = zeroCrossingHz(Array(x[7200..<13000])) // after voice 1's pitch sweep settles
    check(peakDb(x) > -20 && abs(hz - 440) < 10, String(format: "voice 1 plays the restored sample (%.0f Hz, %.1f dB)", hz, peakDb(x)))

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
