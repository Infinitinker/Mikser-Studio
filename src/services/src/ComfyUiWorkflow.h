// The image.generate workflow for ComfyUI: Qwen Image 2.1 text to image, its models, nodes and stages.
#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QStringView>

#include <utility>

namespace mikserstudio::services::comfyui {

// ComfyUI's own text-to-image template for Qwen Image 2.1, without its prompt enhancer: the
// diffusion model through its cache, the Qwen 3 VL text encoder, an empty latent, KSampler (euler,
// simple, cfg 1), the VAE, and SaveImage into a folder of Mikser Studio's own in ComfyUI's output.
// It uses only ComfyUI's local nodes, never its API nodes (whose names end in "Api").

inline constexpr QStringView kDiffusionModel = u"qwen_image_2.1_int8_convrot.safetensors";
inline constexpr QStringView kTextEncoder = u"qwen3vl_8b_int8_convrot.safetensors";
inline constexpr QStringView kVae = u"qwen_image_2.1_vae_bf16.safetensors";
// The folder in ComfyUI's output where the images go, one subfolder per job.
inline constexpr QStringView kOutputFolder = u"mikserstudio";
// The id of the SaveImage node, whose outputs /history lists.
inline constexpr QStringView kSaveNode = u"9";

// What the workflow needs from ComfyUI: (model folder, file), and the nodes not in every ComfyUI.
[[nodiscard]] QList<std::pair<QString, QString>> requiredModels();
[[nodiscard]] QStringList requiredNodes();

// What ComfyUI is doing while it runs a node of the workflow; empty for the others.
[[nodiscard]] QString stage(const QString& nodeId);

// A width or height as the model takes it: rounded down to a multiple of 16.
[[nodiscard]] int modelSize(int pixels);

struct ImageRequest {
    QString prompt;
    QString negativePrompt;
    int width = 1024;
    int height = 1024;
    int steps = 25;
    qint64 seed = 0;
};

// The workflow in ComfyUI's API format: node id -> {class_type, inputs}. SaveImage writes into
// "<filenamePrefix>", such as "mikserstudio/<job id>".
[[nodiscard]] QJsonObject imageWorkflow(const ImageRequest& request, const QString& filenamePrefix);

}  // namespace mikserstudio::services::comfyui
