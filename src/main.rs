use std::path::PathBuf;
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::mpsc::{self, Receiver, TryRecvError};
use std::sync::Arc;
use std::thread;
use std::time::Duration;
#[cfg(target_os = "windows")]
use tinnotus::{
    load_wav_for_playback, AudioPlaybackStatus, OutputDeviceInfo, WindowsAudioPlayback,
};
use tinnotus::{process_wav, WavError};

#[derive(Default)]
struct ProcessingProgress {
    frames_done: AtomicU64,
    total_frames: AtomicU64,
}

struct TinnotusApp {
    input_path: String,
    output_path: String,
    center_frequency_hz: f64,
    bandwidth_hz: f64,
    processing: bool,
    cancellation: Option<Arc<AtomicBool>>,
    progress: Option<Arc<ProcessingProgress>>,
    result_receiver: Option<Receiver<Result<(), WavError>>>,
    status: String,
    #[cfg(target_os = "windows")]
    output_devices: Vec<OutputDeviceInfo>,
    #[cfg(target_os = "windows")]
    output_devices_loaded: bool,
    #[cfg(target_os = "windows")]
    selected_output_device: Option<OutputDeviceInfo>,
    #[cfg(target_os = "windows")]
    playback: Option<WindowsAudioPlayback>,
    #[cfg(target_os = "windows")]
    live_status: String,
}

impl Default for TinnotusApp {
    fn default() -> Self {
        Self {
            input_path: String::new(),
            output_path: String::from("processed.wav"),
            center_frequency_hz: 4000.0,
            bandwidth_hz: 500.0,
            processing: false,
            cancellation: None,
            progress: None,
            result_receiver: None,
            status: String::from("Choose a WAV input and a new output path."),
            #[cfg(target_os = "windows")]
            output_devices: Vec::new(),
            #[cfg(target_os = "windows")]
            output_devices_loaded: false,
            #[cfg(target_os = "windows")]
            selected_output_device: None,
            #[cfg(target_os = "windows")]
            playback: None,
            #[cfg(target_os = "windows")]
            live_status: String::from("Select an output device or use the system default."),
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
        let cancellation = Arc::new(AtomicBool::new(false));
        let worker_cancellation = Arc::clone(&cancellation);
        let progress = Arc::new(ProcessingProgress::default());
        let worker_progress = Arc::clone(&progress);
        let (sender, receiver) = mpsc::channel();
        let worker = thread::Builder::new()
            .name("tinnotus-wav-processor".to_owned())
            .spawn(move || {
                let result = process_wav(
                    &input_path,
                    &output_path,
                    center_frequency_hz,
                    bandwidth_hz,
                    &worker_cancellation,
                    |frames_done, total_frames| {
                        worker_progress
                            .total_frames
                            .store(total_frames, Ordering::Relaxed);
                        worker_progress
                            .frames_done
                            .store(frames_done, Ordering::Relaxed);
                    },
                );
                let _ = sender.send(result);
            });

        match worker {
            Ok(_) => {
                self.processing = true;
                self.cancellation = Some(cancellation);
                self.progress = Some(progress);
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
                self.cancellation = None;
                self.result_receiver = None;
                self.status = "Processed WAV saved.".to_owned();
            }
            Some(Ok(Err(WavError::Cancelled))) => {
                self.processing = false;
                self.cancellation = None;
                self.result_receiver = None;
                self.status = "Processing cancelled; no output was saved.".to_owned();
            }
            Some(Ok(Err(error))) => {
                self.processing = false;
                self.cancellation = None;
                self.result_receiver = None;
                self.status = format!("Audio processing failed: {error}");
            }
            Some(Err(TryRecvError::Disconnected)) => {
                self.processing = false;
                self.cancellation = None;
                self.result_receiver = None;
                self.status = "Audio processing stopped without a result.".to_owned();
            }
            Some(Err(TryRecvError::Empty)) | None => {}
        }
    }

    fn cancel_processing(&mut self) {
        if let Some(cancellation) = &self.cancellation {
            cancellation.store(true, Ordering::Release);
            self.status = "Cancellation requested; the current block will finish first.".to_owned();
        }
    }

    #[cfg(target_os = "windows")]
    fn refresh_output_devices(&mut self) {
        match tinnotus::enumerate_output_devices() {
            Ok(devices) => {
                let selection_lost = self
                    .selected_output_device
                    .as_ref()
                    .map(|selected| !devices.contains(selected))
                    .unwrap_or(false);
                if selection_lost {
                    self.selected_output_device = None;
                }
                self.output_devices = devices;
                self.live_status = if selection_lost {
                    "The selected device is unavailable; the system default is selected. Verify before playback."
                        .to_owned()
                } else if self.output_devices.is_empty() {
                    "No output devices found; connect a device or use the system default."
                        .to_owned()
                } else {
                    "Output devices refreshed.".to_owned()
                };
            }
            Err(error) => {
                self.output_devices.clear();
                self.selected_output_device = None;
                self.live_status = format!("Could not enumerate output devices: {error}");
            }
        }
        self.output_devices_loaded = true;
    }

    #[cfg(target_os = "windows")]
    fn start_live_playback(&mut self) {
        let previous_stop_error = self
            .playback
            .as_mut()
            .and_then(|playback| playback.stop().err())
            .map(|error| error.to_string());
        self.playback = None;

        let input_path = PathBuf::from(self.input_path.trim());
        if input_path.as_os_str().is_empty() {
            self.live_status = "Choose a WAV input before starting playback.".to_owned();
            return;
        }

        let audio = match load_wav_for_playback(&input_path) {
            Ok(audio) => audio,
            Err(error) => {
                self.live_status = format!("Could not load WAV for playback: {error}");
                return;
            }
        };
        match WindowsAudioPlayback::start(
            audio.samples,
            audio.sample_rate,
            audio.channels,
            self.selected_output_device.clone(),
            self.center_frequency_hz,
            self.bandwidth_hz,
        ) {
            Ok(playback) => {
                self.playback = Some(playback);
                self.live_status = match previous_stop_error {
                    Some(error) => format!(
                        "WAV playback started, but closing the previous stream reported: {error}"
                    ),
                    None => "WAV playback started in Windows shared mode.".to_owned(),
                };
            }
            Err(error) => {
                self.live_status = match previous_stop_error {
                    Some(previous_error) => format!(
                        "Could not start WAV playback: {error}. Closing the previous stream also reported: {previous_error}"
                    ),
                    None => format!("Could not start WAV playback: {error}"),
                };
            }
        }
    }

    #[cfg(target_os = "windows")]
    fn stop_live_playback(&mut self) {
        if let Some(playback) = self.playback.as_mut() {
            match playback.stop() {
                Ok(()) => self.live_status = "Playback stopped.".to_owned(),
                Err(error) => self.live_status = format!("Could not stop playback: {error}"),
            }
        }
    }

    #[cfg(target_os = "windows")]
    fn update_live_playback_status(&mut self) {
        let result = self.playback.as_mut().map(WindowsAudioPlayback::poll);
        match result {
            Some(Ok(AudioPlaybackStatus::Finished)) => {
                self.live_status = "Playback finished.".to_owned();
            }
            Some(Ok(AudioPlaybackStatus::DeviceError)) => {
                self.live_status =
                    "The output device stopped or disconnected; refresh devices and restart playback."
                        .to_owned();
            }
            Some(Ok(AudioPlaybackStatus::CallbackError)) => {
                self.live_status =
                    "The audio callback failed; stop playback and retry with a supported device format."
                        .to_owned();
            }
            Some(Ok(AudioPlaybackStatus::Stopped)) => {
                self.live_status = "Playback stopped.".to_owned();
            }
            Some(Ok(AudioPlaybackStatus::Playing)) | None => {}
            Some(Err(error)) => {
                self.live_status = format!("Audio playback polling failed: {error}");
            }
        }
    }

    #[cfg(target_os = "windows")]
    fn update_live_notch(&mut self) {
        if let Some(playback) = self.playback.as_mut() {
            if playback.status() == AudioPlaybackStatus::Playing {
                if let Err(error) =
                    playback.set_notch(0, self.center_frequency_hz, self.bandwidth_hz)
                {
                    self.live_status = format!("Could not update the live notch: {error}");
                }
            }
        }
    }
}

impl eframe::App for TinnotusApp {
    fn update(&mut self, context: &egui::Context, _frame: &mut eframe::Frame) {
        self.update_processing_status();
        if self.processing {
            context.request_repaint_after(Duration::from_millis(50));
        }
        #[cfg(target_os = "windows")]
        {
            if !self.output_devices_loaded {
                self.refresh_output_devices();
            }
            self.update_live_playback_status();
        }

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
            let mut _notch_changed = false;
            _notch_changed |= ui
                .add(
                egui::Slider::new(&mut self.center_frequency_hz, 100.0..=8000.0)
                    .text("Notch center (Hz)"),
            )
                .changed();
            _notch_changed |= ui
                .add(
                egui::Slider::new(&mut self.bandwidth_hz, 50.0..=2000.0)
                    .text("Bandwidth (Hz)"),
            )
                .changed();
            #[cfg(target_os = "windows")]
            if _notch_changed {
                self.update_live_notch();
            }

            ui.label("Supports 16/24/32-bit PCM and 32-bit floating-point WAV.");
            ui.label("Processing is block-based; live playback is available on Windows.");
            ui.add_space(8.0);

            if self.processing {
                if ui.button("Cancel").clicked() {
                    self.cancel_processing();
                }
            } else if ui.button("Process WAV").clicked() {
                self.start_processing();
            }
            if let Some(progress) = &self.progress {
                let total_frames = progress.total_frames.load(Ordering::Relaxed);
                if total_frames > 0 {
                    let frames_done = progress
                        .frames_done
                        .load(Ordering::Relaxed)
                        .min(total_frames);
                    let fraction = (frames_done as f32 / total_frames as f32).clamp(0.0, 1.0);
                    ui.add(
                        egui::ProgressBar::new(fraction)
                            .show_percentage()
                            .text(format!("{frames_done} / {total_frames} frames")),
                    );
                }
            }

            ui.label(self.status.as_str());

            #[cfg(target_os = "windows")]
            {
                ui.separator();
                ui.heading("Live WAV playback");
                ui.label("Windows WASAPI shared mode; WAV decoding and format conversion happen before playback.");
                let selected_name = self
                    .selected_output_device
                    .as_ref()
                    .map(|device| device.name.clone())
                    .unwrap_or_else(|| "System default".to_owned());
                egui::ComboBox::from_label("Output device")
                    .selected_text(selected_name)
                    .show_ui(ui, |ui| {
                        ui.selectable_value(
                            &mut self.selected_output_device,
                            None,
                            "System default",
                        );
                        for device in &self.output_devices {
                            ui.selectable_value(
                                &mut self.selected_output_device,
                                Some(device.clone()),
                                &device.name,
                            );
                        }
                    });

                let playback_active = self
                    .playback
                    .as_ref()
                    .map(|playback| playback.status() == AudioPlaybackStatus::Playing)
                    .unwrap_or(false);
                ui.horizontal(|ui| {
                    if ui
                        .add_enabled(!playback_active, egui::Button::new("Refresh devices"))
                        .clicked()
                    {
                        self.refresh_output_devices();
                    }
                    if ui
                        .add_enabled(
                            !playback_active,
                            egui::Button::new("Play input WAV through device"),
                        )
                        .clicked()
                    {
                        self.start_live_playback();
                    }
                    if ui
                        .add_enabled(playback_active, egui::Button::new("Stop playback"))
                        .clicked()
                    {
                        self.stop_live_playback();
                    }
                });
                ui.label(self.live_status.as_str());
                ui.label("Current source pitch (analyzed off the audio callback):");
                if let Some(playback) = &self.playback {
                    if playback.pitch_analysis_failed() {
                        ui.label("Pitch analysis failed.");
                    } else if let Some(estimate) = playback.pitch_estimate() {
                        if let Some(frequency_hz) = estimate.frequency_hz {
                            ui.label(format!(
                                "{frequency_hz:.1} Hz (confidence {:.2})",
                                estimate.confidence
                            ));
                        } else {
                            ui.label(format!(
                                "No stable pitch detected (confidence {:.2})",
                                estimate.confidence
                            ));
                        }
                    } else {
                        ui.label("Waiting for enough audio to estimate pitch...");
                    }
                } else {
                    ui.label("Start WAV playback to display an estimate.");
                }
                if let Some(playback) = &self.playback {
                    ui.label(format!(
                        "Callback cadence misses (possible output underruns): {}",
                        playback.callback_cadence_misses()
                    ));
                }
            }
            #[cfg(not(target_os = "windows"))]
            ui.label("Live device playback is currently supported on Windows only.");
        });

        #[cfg(target_os = "windows")]
        if self
            .playback
            .as_ref()
            .map(|playback| playback.status() == AudioPlaybackStatus::Playing)
            .unwrap_or(false)
        {
            context.request_repaint_after(std::time::Duration::from_millis(100));
        }
    }
}

fn main() -> eframe::Result<()> {
    eframe::run_native(
        "Tinnotus - Experimental Audio Processor",
        eframe::NativeOptions::default(),
        Box::new(|_creation_context| Box::new(TinnotusApp::default())),
    )
}
