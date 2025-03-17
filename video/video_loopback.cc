/*
 *  Copyright (c) 2015 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include "video/video_loopback.h"

#include <stdio.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "api/test/simulated_network.h"
#include "api/test/video_quality_test_fixture.h"
#include "api/transport/bitrate_settings.h"
#include "api/units/data_rate.h"
#include "api/video_codecs/video_codec.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "system_wrappers/include/field_trial.h"
#include "test/field_trial.h"
#include "test/gtest.h"
#include "test/run_test.h"
#include "test/test_flags.h"
#include "video/video_quality_test.h"

// Flags common with screenshare loopback, with different default values.
ABSL_FLAG(int, width, 640, "Video width.");

ABSL_FLAG(int, height, 480, "Video height.");

ABSL_FLAG(int, fps, 30, "Frames per second.");

ABSL_FLAG(int, capture_device_index, 0, "Capture device to select");

ABSL_FLAG(int, min_bitrate, 50, "Call and stream min bitrate in kbps.");

ABSL_FLAG(int, start_bitrate, 300, "Call start bitrate in kbps.");

ABSL_FLAG(int, target_bitrate, 800, "Stream target bitrate in kbps.");

ABSL_FLAG(int, max_bitrate, 800, "Call and stream max bitrate in kbps.");

ABSL_FLAG(bool,
          suspend_below_min_bitrate,
          false,
          "Suspends video below the configured min bitrate.");

ABSL_FLAG(int,
          num_temporal_layers,
          1,
          "Number of temporal layers. Set to 1-4 to override.");

ABSL_FLAG(int,
          inter_layer_pred,
          2,
          "Inter-layer prediction mode. "
          "0 - enabled, 1 - disabled, 2 - enabled only for key pictures.");

// Flags common with screenshare loopback, with equal default values.
ABSL_FLAG(std::string, codec, "VP8", "Video codec to use.");

ABSL_FLAG(int,
          selected_tl,
          -1,
          "Temporal layer to show or analyze. -1 to disable filtering.");

ABSL_FLAG(
    int,
    duration,
    0,
    "Duration of the test in seconds. If 0, rendered will be shown instead.");

ABSL_FLAG(std::string, output_filename, "", "Target graph data filename.");

ABSL_FLAG(std::string,
          graph_title,
          "",
          "If empty, title will be generated automatically.");

ABSL_FLAG(int, loss_percent, 0, "Percentage of packets randomly lost.");

ABSL_FLAG(int,
          avg_burst_loss_length,
          -1,
          "Average burst length of lost packets.");

ABSL_FLAG(int,
          link_capacity,
          0,
          "Capacity (kbps) of the fake link. 0 means infinite.");

ABSL_FLAG(int, queue_size, 0, "Size of the bottleneck link queue in packets.");

ABSL_FLAG(int,
          avg_propagation_delay_ms,
          0,
          "Average link propagation delay in ms.");

ABSL_FLAG(std::string,
          rtc_event_log_name,
          "",
          "Filename for rtc event log. Two files "
          "with \"_send\" and \"_recv\" suffixes will be created.");

ABSL_FLAG(std::string,
          rtp_dump_name,
          "",
          "Filename for dumped received RTP stream.");

ABSL_FLAG(int,
          std_propagation_delay_ms,
          0,
          "Link propagation delay standard deviation in ms.");

ABSL_FLAG(int, num_streams, 0, "Number of streams to show or analyze.");

ABSL_FLAG(int,
          selected_stream,
          0,
          "ID of the stream to show or analyze. "
          "Set to the number of streams to show them all.");

ABSL_FLAG(int, num_spatial_layers, 1, "Number of spatial layers to use.");

ABSL_FLAG(int,
          selected_sl,
          -1,
          "Spatial layer to show or analyze. -1 to disable filtering.");

ABSL_FLAG(std::string,
          stream0,
          "",
          "Comma separated values describing VideoStream for stream #0.");

ABSL_FLAG(std::string,
          stream1,
          "",
          "Comma separated values describing VideoStream for stream #1.");

ABSL_FLAG(std::string,
          sl0,
          "",
          "Comma separated values describing SpatialLayer for layer #0.");

ABSL_FLAG(std::string,
          sl1,
          "",
          "Comma separated values describing SpatialLayer for layer #1.");

ABSL_FLAG(std::string,
          sl2,
          "",
          "Comma separated values describing SpatialLayer for layer #2.");

ABSL_FLAG(std::string,
          encoded_frame_path,
          "",
          "The base path for encoded frame logs. Created files will have "
          "the form <encoded_frame_path>.<n>.(recv|send.<m>).ivf");

ABSL_FLAG(bool, logs, false, "print logs to stderr");

ABSL_FLAG(bool, send_side_bwe, true, "Use send-side bandwidth estimation");

ABSL_FLAG(bool, generic_descriptor, false, "Use the generic frame descriptor.");

ABSL_FLAG(bool, dependency_descriptor, false, "Use the dependency descriptor.");

ABSL_FLAG(bool, allow_reordering, false, "Allow packet reordering to occur");

ABSL_FLAG(bool, use_ulpfec, false, "Use RED+ULPFEC forward error correction.");

ABSL_FLAG(bool, use_flexfec, false, "Use FlexFEC forward error correction.");

ABSL_FLAG(bool, audio, false, "Add audio stream");

ABSL_FLAG(bool,
          use_real_adm,
          false,
          "Use real ADM instead of fake (no effect if audio is false)");

ABSL_FLAG(bool,
          audio_video_sync,
          false,
          "Sync audio and video stream (no effect if"
          " audio is false)");

ABSL_FLAG(bool,
          audio_dtx,
          false,
          "Enable audio DTX (no effect if audio is false)");

ABSL_FLAG(bool, video, true, "Add video stream");

ABSL_FLAG(std::string,
          clip,
          "",
          "Name of the clip to show. If empty, using chroma generator.");

namespace webrtc {
namespace {

size_t Width() {
  return static_cast<size_t>(absl::GetFlag(FLAGS_width));
}

size_t Height() {
  return static_cast<size_t>(absl::GetFlag(FLAGS_height));
}

int Fps() {
  return absl::GetFlag(FLAGS_fps);
}

size_t GetCaptureDevice() {
  return static_cast<size_t>(absl::GetFlag(FLAGS_capture_device_index));
}

int MinBitrateKbps() {
  return absl::GetFlag(FLAGS_min_bitrate);
}

int StartBitrateKbps() {
  return absl::GetFlag(FLAGS_start_bitrate);
}

int TargetBitrateKbps() {
  return absl::GetFlag(FLAGS_target_bitrate);
}

int MaxBitrateKbps() {
  return absl::GetFlag(FLAGS_max_bitrate);
}

int NumTemporalLayers() {
  return absl::GetFlag(FLAGS_num_temporal_layers);
}

InterLayerPredMode InterLayerPred() {
  if (absl::GetFlag(FLAGS_inter_layer_pred) == 0) {
    return InterLayerPredMode::kOn;
  } else if (absl::GetFlag(FLAGS_inter_layer_pred) == 1) {
    return InterLayerPredMode::kOff;
  } else {
    RTC_DCHECK_EQ(absl::GetFlag(FLAGS_inter_layer_pred), 2);
    return InterLayerPredMode::kOnKeyPic;
  }
}

std::string Codec() {
  return absl::GetFlag(FLAGS_codec);
}

int SelectedTL() {
  return absl::GetFlag(FLAGS_selected_tl);
}

int DurationSecs() {
  return absl::GetFlag(FLAGS_duration);
}

std::string OutputFilename() {
  return absl::GetFlag(FLAGS_output_filename);
}

std::string GraphTitle() {
  return absl::GetFlag(FLAGS_graph_title);
}

int LossPercent() {
  return static_cast<int>(absl::GetFlag(FLAGS_loss_percent));
}

int AvgBurstLossLength() {
  return static_cast<int>(absl::GetFlag(FLAGS_avg_burst_loss_length));
}

DataRate LinkCapacity() {
  int link_capacity_kbps = absl::GetFlag(FLAGS_link_capacity);
  return link_capacity_kbps == 0 ? DataRate::Infinity()
                                 : DataRate::KilobitsPerSec(link_capacity_kbps);
}

int QueueSize() {
  return static_cast<int>(absl::GetFlag(FLAGS_queue_size));
}

int AvgPropagationDelayMs() {
  return static_cast<int>(absl::GetFlag(FLAGS_avg_propagation_delay_ms));
}

std::string RtcEventLogName() {
  return absl::GetFlag(FLAGS_rtc_event_log_name);
}

std::string RtpDumpName() {
  return absl::GetFlag(FLAGS_rtp_dump_name);
}

int StdPropagationDelayMs() {
  return absl::GetFlag(FLAGS_std_propagation_delay_ms);
}

int NumStreams() {
  return absl::GetFlag(FLAGS_num_streams);
}

int SelectedStream() {
  return absl::GetFlag(FLAGS_selected_stream);
}

int NumSpatialLayers() {
  return absl::GetFlag(FLAGS_num_spatial_layers);
}

int SelectedSL() {
  return absl::GetFlag(FLAGS_selected_sl);
}

std::string Stream0() {
  return absl::GetFlag(FLAGS_stream0);
}

std::string Stream1() {
  return absl::GetFlag(FLAGS_stream1);
}

std::string SL0() {
  return absl::GetFlag(FLAGS_sl0);
}

std::string SL1() {
  return absl::GetFlag(FLAGS_sl1);
}

std::string SL2() {
  return absl::GetFlag(FLAGS_sl2);
}

std::string EncodedFramePath() {
  return absl::GetFlag(FLAGS_encoded_frame_path);
}

std::string Clip() {
  return absl::GetFlag(FLAGS_clip);
}

}  // namespace
void Loopback() {
  printf("======================= 开始网络回环测试 =========================\n");
  
  // 网络管道配置
  printf("[配置] 初始化网络行为参数...\n");
  BuiltInNetworkBehaviorConfig pipe_config;
  pipe_config.loss_percent = LossPercent();
  printf("  丢包率: %.2f%%\n", pipe_config.loss_percent);
  pipe_config.avg_burst_loss_length = AvgBurstLossLength();
  printf("  平均突发丢包长度: %d 包\n", pipe_config.avg_burst_loss_length);
  pipe_config.link_capacity = LinkCapacity();
  // printf("  链路容量: %.2f Mbps\n", pipe_config.link_capacity / 1e6);
  pipe_config.queue_length_packets = QueueSize();
  // printf("  队列长度: %d 包\n", pipe_config.queue_length_packets);
  pipe_config.queue_delay_ms = AvgPropagationDelayMs();
  printf("  平均队列延迟: %d ms\n", pipe_config.queue_delay_ms);
  pipe_config.delay_standard_deviation_ms = StdPropagationDelayMs();
  printf("  延迟标准差: %d ms\n", pipe_config.delay_standard_deviation_ms);
  pipe_config.allow_reordering = absl::GetFlag(FLAGS_allow_reordering);
  printf("  允许乱序: %s\n", pipe_config.allow_reordering ? "是" : "否");
  printf("[配置] 网络行为参数初始化完成\n\n");

  // 呼叫比特率配置
  printf("[配置] 设置呼叫比特率参数...\n");
  BitrateConstraints call_bitrate_config;
  call_bitrate_config.min_bitrate_bps = MinBitrateKbps() * 1000;
  printf("  最小比特率: %d Kbps\n", call_bitrate_config.min_bitrate_bps / 1000);
  call_bitrate_config.start_bitrate_bps = StartBitrateKbps() * 1000;
  printf("  起始比特率: %d Kbps\n", call_bitrate_config.start_bitrate_bps / 1000);
  call_bitrate_config.max_bitrate_bps = -1;  // 不限制带宽估计
  printf("  最大比特率: 无限制\n");
  printf("[配置] 比特率参数设置完成\n\n");

  // 主测试参数配置
  printf("[配置] 初始化视频质量测试参数...\n");
  VideoQualityTest::Params params;
  
  // 呼叫配置
  params.call.send_side_bwe = absl::GetFlag(FLAGS_send_side_bwe);
  printf("  发送端带宽估计: %s\n", params.call.send_side_bwe ? "启用" : "禁用");
  params.call.generic_descriptor = absl::GetFlag(FLAGS_generic_descriptor);
  printf("  通用描述符: %s\n", params.call.generic_descriptor ? "启用" : "禁用");
  params.call.dependency_descriptor = absl::GetFlag(FLAGS_dependency_descriptor);
  printf("  依赖描述符: %s\n", params.call.dependency_descriptor ? "启用" : "禁用");
  params.call.call_bitrate_config = call_bitrate_config;
  printf("  呼叫比特率配置已绑定\n");

  // 视频配置
  printf("\n[视频] 配置视频流参数...\n");
  params.video[0].enabled = absl::GetFlag(FLAGS_video);
  printf("  视频流启用: %s\n", params.video[0].enabled ? "是" : "否");
  if (params.video[0].enabled) {
    params.video[0].width = Width();
    params.video[0].height = Height();
    // printf("  分辨率: %dx%d\n", params.video[0].width, params.video[0].height);
    params.video[0].fps = Fps();
    printf("  帧率: %d FPS\n", params.video[0].fps);
    // printf("  比特率范围: %d Kbps \~ %d Kbps\n", 
    //        params.video[0].min_bitrate_bps / 1000, 
    //        params.video[0].max_bitrate_bps / 1000);
    params.video[0].suspend_below_min_bitrate = absl::GetFlag(FLAGS_suspend_below_min_bitrate);
    printf("  低于最小比特率暂停: %s\n", params.video[0].suspend_below_min_bitrate ? "是" : "否");
    params.video[0].codec = Codec();
    printf("  编解码器: %s\n", params.video[0].codec.c_str());
    params.video[0].num_temporal_layers = NumTemporalLayers();
    printf("  时间层数: %d\n", params.video[0].num_temporal_layers);
    params.video[0].ulpfec = absl::GetFlag(FLAGS_use_ulpfec);
    printf("  ULPFEC 纠错: %s\n", params.video[0].ulpfec ? "启用" : "禁用");
    params.video[0].flexfec = absl::GetFlag(FLAGS_use_flexfec);
    printf("  FlexFEC 纠错: %s\n", params.video[0].flexfec ? "启用" : "禁用");
  }
  printf("[视频] 参数配置完成\n\n");

  params.video[0].enabled = absl::GetFlag(FLAGS_video);
  params.video[0].width = Width();
  params.video[0].height = Height();
  params.video[0].fps = Fps();
  params.video[0].min_bitrate_bps = MinBitrateKbps() * 1000;
  params.video[0].target_bitrate_bps = TargetBitrateKbps() * 1000;
  params.video[0].max_bitrate_bps = MaxBitrateKbps() * 1000;
  params.video[0].suspend_below_min_bitrate =
      absl::GetFlag(FLAGS_suspend_below_min_bitrate);
  params.video[0].codec = Codec();
  params.video[0].num_temporal_layers = NumTemporalLayers();
  params.video[0].selected_tl = SelectedTL();
  params.video[0].min_transmit_bps = 0;
  params.video[0].ulpfec = absl::GetFlag(FLAGS_use_ulpfec);
  params.video[0].flexfec = absl::GetFlag(FLAGS_use_flexfec);
  params.video[0].automatic_scaling = NumStreams() < 2;
  params.video[0].clip_path = Clip();
  params.video[0].capture_device_index = GetCaptureDevice();
  // 音频配置
  printf("[音频] 配置音频流参数...\n");
  params.audio.enabled = absl::GetFlag(FLAGS_audio);
  printf("  音频流启用: %s\n", params.audio.enabled ? "是" : "否");
  if (params.audio.enabled) {
    params.audio.sync_video = absl::GetFlag(FLAGS_audio_video_sync);
    printf("  音视频同步: %s\n", params.audio.sync_video ? "启用" : "禁用");
    params.audio.dtx = absl::GetFlag(FLAGS_audio_dtx);
    printf("  不连续传输 (DTX): %s\n", params.audio.dtx ? "启用" : "禁用");
    params.audio.use_real_adm = absl::GetFlag(FLAGS_use_real_adm);
    printf("  使用真实音频设备: %s\n", params.audio.use_real_adm ? "是" : "否");
  }
  printf("[音频] 参数配置完成\n\n");

  // 日志与诊断配置
  printf("[日志] 配置诊断输出...\n");
  params.logging.rtc_event_log_name = RtcEventLogName();
  printf("  RTC 事件日志文件: %s\n", params.logging.rtc_event_log_name.c_str());
  params.logging.rtp_dump_name = RtpDumpName();
  printf("  RTP 数据包转储文件: %s\n", params.logging.rtp_dump_name.c_str());
  params.logging.encoded_frame_base_path = EncodedFramePath();
  printf("  编码帧存储路径: %s\n", params.logging.encoded_frame_base_path.c_str());
  printf("[日志] 配置完成\n\n");

  // 测试分析器配置
  printf("[分析器] 初始化测试分析参数...\n");
  params.analyzer.test_label = "video";
  printf("  测试标签: %s\n", params.analyzer.test_label.c_str());
  params.analyzer.test_durations_secs = DurationSecs();
  printf("  测试持续时间: %d 秒\n", params.analyzer.test_durations_secs);
  params.analyzer.graph_data_output_filename = OutputFilename();
  printf("  图表数据输出文件: %s\n", params.analyzer.graph_data_output_filename.c_str());
  params.analyzer.graph_title = GraphTitle();
  printf("  图表标题: %s\n", params.analyzer.graph_title.c_str());
  printf("[分析器] 配置完成\n\n");

  // 最终配置绑定
  printf("[系统] 绑定网络管道配置...\n");
  params.config = pipe_config;
  printf("======================= 配置完成，准备启动测试 =========================\n");
  // 流推断逻辑
  printf("\n[流推断] 检查流推断条件...\n");
  printf("  当前流数量: %d\n", NumStreams());
  printf("  Stream0 内容: '%s' (长度: %zu)\n", Stream0().c_str(), Stream0().size());
  printf("  Stream1 内容: '%s' (长度: %zu)\n", Stream1().c_str(), Stream1().size());
  
  if (NumStreams() > 1 && Stream0().empty() && Stream1().empty()) {
    printf("  满足流推断条件，启用自动流推断\n");
    params.ss[0].infer_streams = true;
  } else {
    printf("  不满足流推断条件，跳过自动推断\n");
  }

  // 流描述符配置
  printf("\n[流配置] 初始化流描述符...\n");
  std::vector<std::string> stream_descriptors;
  stream_descriptors.push_back(Stream0());
  stream_descriptors.push_back(Stream1());
  printf("  已添加流描述符: \n");
  for (const auto& desc : stream_descriptors) {
    printf("    - %s\n", desc.empty() ? "<空>" : desc.c_str());
  }

  // 空间层描述符配置
  printf("\n[空间层] 初始化空间层描述符...\n");
  std::vector<std::string> SL_descriptors;
  SL_descriptors.push_back(SL0());
  SL_descriptors.push_back(SL1());
  SL_descriptors.push_back(SL2());
  printf("  已添加空间层描述符: \n");
  for (size_t i = 0; i < SL_descriptors.size(); ++i) {
    printf("    SL%d: %s\n", static_cast<int>(i), 
           SL_descriptors[i].empty() ? "<未配置>" : SL_descriptors[i].c_str());
  }

  // 可伸缩性配置
  printf("\n[可伸缩性] 填充参数到 VideoQualityTest...\n");
  VideoQualityTest fixture(nullptr);
  printf("  流数量: %d\n  选定流索引: %d\n  空间层数: %d\n  选定空间层: %d\n",
         NumStreams(), SelectedStream(), NumSpatialLayers(), SelectedSL());
  // printf("  层间预测模式: %s\n", InterLayerPred().c_str());
  
  fixture.FillScalabilitySettings(
      &params, 0, stream_descriptors, NumStreams(), SelectedStream(),
      NumSpatialLayers(), SelectedSL(), InterLayerPred(), SL_descriptors);
  printf("  可伸缩性配置完成\n");

  // 测试执行分支
  printf("\n[执行] 选择测试运行模式...\n");
  if (DurationSecs()) {
    printf("  使用分析器模式运行，持续时间: %d 秒\n", DurationSecs());
    printf("  输出文件: %s\n", params.analyzer.graph_data_output_filename.c_str());
    fixture.RunWithAnalyzer(params);
  } else {
    printf("  使用渲染器模式运行（无时间限制）\n");
    fixture.RunWithRenderers(params);
  }
  printf("======================= 测试启动完成 =========================\n");

}

int RunLoopbackTest(int argc, char* argv[]) {
  ::testing::InitGoogleTest(&argc, argv);
  absl::ParseCommandLine(argc, argv);

  rtc::LogMessage::SetLogToStderr(absl::GetFlag(FLAGS_logs));

  // InitFieldTrialsFromString stores the char*, so the char array must outlive
  // the application.
  const std::string field_trials = absl::GetFlag(FLAGS_force_fieldtrials);
  webrtc::field_trial::InitFieldTrialsFromString(field_trials.c_str());

  webrtc::test::RunTest(webrtc::Loopback);
  return 0;
}
}  // namespace webrtc
