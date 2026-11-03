use std::path::PathBuf;
use std::sync::mpsc::{self, Receiver, TryRecvError};
use std::thread;
use tinnotus::process_wav;

struct TinnotusApp {
    input_path: String,
    output_path: String,
    center_frequency_hz: f64,
    bandwidth_hz: f64,
    processing: bool,
    result_receiver: Option<Receiver<Result<(), String>>>,
    status: String,
}

impl Default for TinnotusApp {
    fn default() -> Self {
        Self {
            input_path: String::new(),
            output_path: String::from("processed.wav"),
            center_frequency_hz: 4000.0,
            bandwidth_hz: 500.0,
            processing: false,
            result_receiver: None,
            status: String::from("Choose a WAV input and a new output path."),
        }
    }
}

impl TinnotusApp {
    fn start_processing(&mut self) {
        let input_path = PathBuf::from(self.input_path.trim());
        let output_path = PathBuf::from(self.output_path.trim());
        if input_path.as_os_str().is_empty() || output_path.as_os_str().is_empty() {
            self.status = "Input and output paths are required.".to_owned();
            return;
        }

        let center_frequency_hz = self.center_frequency_hz;
        let bandwidth_hz = self.bandwidth_hz;
        let (sender, receiver) = mpsc::channel();
        let worker = thread::Builder::new()
            .name("tinnotus-wav-processor".to_owned())
            .spawn(move || {
                let result = process_wav(
                    &input_path,
                    &output_path,
                    center_frequency_hz,
                    bandwidth_hz,
                );
                let _ = sender.send(result);
            });

        match worker {
            Ok(_) => {
                self.processing = true;
                self.result_receiver = Some(receiver);
                self.status = "Processing WAV...".to_owned();
            }
            Err(error) => {
                self.status = format!("Could not start audio processing: {error}");
            }
        }
    }

    fn update_processing_status(&mut self) {
        let result = self
            .result_receiver
            .as_ref()
            .map(|receiver| receiver.try_recv());

        match result {
            Some(Ok(Ok(()))) => {
                self.processing = false;
                self.result_receiver = None;
                self.status = "Processed WAV saved.".to_owned();
            }
            Some(Ok(Err(error))) => {
                self.processing = false;
                self.result_receiver = None;
                self.status = format!("Audio processing failed: {error}");
            }
            Some(Err(TryRecvError::Disconnected)) => {
                self.processing = false;
                self.result_receiver = None;
                self.status = "Audio processing stopped without a result.".to_owned();
            }
            Some(Err(TryRecvError::Empty)) | None => {}
        }
    }
}

impl eframe::App for TinnotusApp {
    fn update(&mut self, context: &egui::Context, _frame: &mut eframe::Frame) {
        self.update_processing_status();

        egui::CentralPanel::default().show(context, |ui| {
            ui.heading("Tinnotus audio processor");
            ui.label(
                "Experimental audio processing only; this is not a medical device or treatment.",
            );
            ui.add_space(12.0);

            ui.label("Input WAV");
            ui.text_edit_singleline(&mut self.input_path);
            ui.label("Output WAV (must not already exist)");
            ui.text_edit_singleline(&mut self.output_path);

            ui.add_space(8.0);
            ui.add(
                egui::Slider::new(&mut self.center_frequency_hz, 100.0..=8000.0)
                    .text("Notch center (Hz)"),
            );
            ui.add(
                egui::Slider::new(&mut self.bandwidth_hz, 50.0..=2000.0)
                    .text("Bandwidth (Hz)"),
            );

            ui.label("Supports 16/24/32-bit PCM and 32-bit floating-point WAV.");
            ui.label("Processing is block-based; hardware playback is not available yet.");
            ui.add_space(8.0);

            if ui
                .add_enabled(!self.processing, egui::Button::new("Process WAV"))
                .clicked()
            {
                self.start_processing();
            }

            ui.label(self.status.as_str());
        });
    }
}

fn main() -> eframe::Result<()> {
    eframe::run_native(
        "Tinnotus - Experimental Audio Processor",
        eframe::NativeOptions::default(),
        Box::new(|_creation_context| Box::new(TinnotusApp::default())),
    )
}
