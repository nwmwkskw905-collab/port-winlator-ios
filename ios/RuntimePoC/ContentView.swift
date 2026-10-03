//
//  ContentView.swift — diagnostic UI for the reconstructed Phase 02 runtime PoC.
//
//  PHASE_02_RECONSTRUCTED_POC. This is a diagnostics screen, not the Winlator UI:
//  run the suites, read the report, copy it, share it.
//

import SwiftUI
import UniformTypeIdentifiers

struct ContentView: View {
    @State private var report: String = ""
    @State private var isRunning = false
    @State private var status: String = "idle"
    @State private var suite: String = "all"

    private var suites: [String] {
        let names = Phase02Bridge.suiteNames()
        return ["all"] + names.split(separator: ",").map(String.init)
    }

    private var workdir: String {
        NSTemporaryDirectory()
    }

    var body: some View {
        NavigationView {
            VStack(alignment: .leading, spacing: 8) {
                Text("Phase 02 Reconstructed Runtime PoC")
                    .font(.headline)
                Text("PHASE_02_RECONSTRUCTED_POC — host results are not iOS results.")
                    .font(.caption)
                    .foregroundColor(.secondary)

                Text(Phase02Bridge.platformDescription())
                    .font(.caption2)
                    .foregroundColor(.secondary)

                Picker("Suite", selection: $suite) {
                    ForEach(suites, id: \.self) { name in
                        Text(name).tag(name)
                    }
                }
                .pickerStyle(MenuPickerStyle())

                HStack {
                    Button("Run All Tests") { run("all") }
                        .disabled(isRunning)
                    Button("Run Selected") { run(suite) }
                        .disabled(isRunning)
                }

                Text("status: \(status)").font(.footnote)

                ScrollView {
                    Text(report.isEmpty ? "No report yet." : report)
                        .font(.system(.caption, design: .monospaced))
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .textSelection(.enabled)
                }
                .border(Color.gray.opacity(0.3))

                HStack {
                    Button("Copy report") {
                        UIPasteboard.general.string = report
                        status = "copied \(report.count) characters"
                    }
                    .disabled(report.isEmpty)

                    Button("Save report") { save() }
                        .disabled(report.isEmpty)

                    ShareLink(item: report) {
                        Text("Share")
                    }
                    .disabled(report.isEmpty)
                }
            }
            .padding()
            .navigationTitle("Runtime PoC")
        }
        .navigationViewStyle(StackNavigationViewStyle())
    }

    private func run(_ name: String) {
        isRunning = true
        status = "running \(name)…"
        // The harness is synchronous C; running it off the main thread keeps the UI
        // responsive during the fault-probing suites.
        DispatchQueue.global(qos: .userInitiated).async {
            let text = (name == "all")
                ? Phase02Bridge.runAll(withWorkdir: workdir)
                : Phase02Bridge.runSuite(name, workdir: workdir)
            DispatchQueue.main.async {
                report = text
                isRunning = false
                status = Phase02Bridge.lastRunHadFailure() ? "finished with FAIL" : "finished"
            }
        }
    }

    private func save() {
        let name = Phase02Bridge.reportFileName()
        // File sharing (UIFileSharingEnabled + LSSupportsOpeningDocumentsInPlace) exposes the
        // app's Documents directory, not tmp/: the suites keep using `workdir` (tmp) for their
        // scratch files, and the saved report goes where it can actually be recovered.
        guard let documents = FileManager.default.urls(for: .documentDirectory,
                                                      in: .userDomainMask).first else {
            status = "save failed: no Documents directory"
            return
        }
        let url = documents.appendingPathComponent(name)
        do {
            try report.write(to: url, atomically: true, encoding: .utf8)
            status = "saved to \(url.lastPathComponent)"
        } catch {
            status = "save failed: \(error.localizedDescription)"
        }
    }
}

struct ContentView_Previews: PreviewProvider {
    static var previews: some View {
        ContentView()
    }
}
