// The AI engine against fake Ollama and ComfyUI servers: text and image jobs with their provenance, the
// ComfyUI workflow, availability, one job at a time per backend, canceling, and settings that apply at once.
#include "FakeAiTools.h"
#include "ServicesTestSupport.h"
#include "mikserstudio/services/AiEngine.h"

#include "OllamaBackend.h"

#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>

#include <algorithm>

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;
using namespace mikserstudio;
using namespace mikserstudio::services;
using mikserstudio::tests::FakeComfyUi;
using mikserstudio::tests::FakeOllama;
using mikserstudio::tests::waitFor;

namespace {

struct EngineFixture {
    QTemporaryDir dir;
    FakeOllama ollama;
    FakeComfyUi comfy;
    std::unique_ptr<AiEngine> engine;

    EngineFixture()
    {
        engine = std::make_unique<AiEngine>(AiEngineOptions{
            .cacheDirectory = dir.filePath(u"cache"_s),
            .settingsFile = dir.filePath(u"ai-settings.json"_s),
        });
        REQUIRE(engine->setSettings(AiSettings{.ollamaUrl = ollama.url(), .comfyuiUrl = comfy.url()}));
    }

    QString submit(const QString& op, const QJsonObject& params, const QString& backend = {}) const
    {
        auto id = engine->submit(op, params, backend, {});
        if (!id)
            FAIL("submit failed: " << id.error().message.toStdString());
        return *id;
    }

    QJsonObject job(const QString& id) const
    {
        for (const QJsonValue value : engine->jobsJson()) {
            if (value.toObject().value(u"id").toString() == id)
                return value.toObject();
        }
        return {};
    }

    QString state(const QString& id) const { return job(id).value(u"state").toString(); }

    QJsonObject waitFinished(const QString& id) const
    {
        REQUIRE(waitFor([&] {
            const QString now = state(id);
            return now == u"succeeded" || now == u"failed" || now == u"canceled";
        }));
        return job(id);
    }
};

bool usesApiNodes(const QJsonObject& workflow)
{
    return std::ranges::any_of(workflow, [](const QJsonValue& node) {
        return node.toObject().value(u"class_type").toString().endsWith(u"Api"_s);
    });
}

}  // namespace

TEST_CASE("text.generate uses the first model that can complete and drops its reasoning", "[services][ai]")
{
    EngineFixture fixture;
    const QString id = fixture.submit(u"text.generate"_s, {{u"prompt"_s, u"Say hello"_s}});
    const QJsonObject job = fixture.waitFinished(id);
    INFO(job.value(u"error").toString().toStdString());
    REQUIRE(job.value(u"state").toString() == u"succeeded"_s);
    CHECK(job.value(u"backend").toString() == u"ollama"_s);
    CHECK(job.value(u"title").toString() == u"Generate text"_s);

    const QJsonObject result = job.value(u"result").toObject();
    CHECK(result.value(u"text").toString() == u"Hello there."_s);
    CHECK(result.value(u"model").toString() == u"llama3:latest"_s);
    CHECK(result.value(u"outputs").toArray().isEmpty());

    const QJsonObject provenance = result.value(u"provenance").toObject();
    CHECK(provenance.value(u"jobId").toString() == id);
    CHECK(provenance.value(u"backend").toString() == u"ollama"_s);
    CHECK(provenance.value(u"op").toString() == u"text.generate"_s);
    CHECK(provenance.value(u"params").toObject().value(u"prompt").toString() == u"Say hello"_s);
    CHECK_FALSE(provenance.value(u"createdAt").toString().isEmpty());
    CHECK(provenance.value(u"tools").toObject().value(u"ollama").toString() == u"0.12.3"_s);
    const QJsonObject model = provenance.value(u"model").toObject();
    CHECK(model.value(u"name").toString() == u"llama3:latest"_s);
    CHECK(model.value(u"digest").toString() == u"sha256:llama3:latest"_s);
    CHECK(model.value(u"family").toString() == u"llama"_s);
    CHECK(model.value(u"parameterSize").toString() == u"8.0B"_s);
    CHECK(model.value(u"quantization").toString() == u"Q4_K_M"_s);

    const auto requests = fixture.ollama.generateRequests();
    REQUIRE(requests.size() == 1);
    CHECK(requests.first().value(u"model").toString() == u"llama3:latest"_s);
    CHECK(requests.first().value(u"stream").toBool());
}

TEST_CASE("text.generate refuses a model that is missing or cannot generate text", "[services][ai]")
{
    EngineFixture fixture;
    const QJsonObject missing = fixture.waitFinished(
        fixture.submit(u"text.generate"_s, {{u"prompt"_s, u"Hi"_s}, {u"model"_s, u"nope"_s}}));
    CHECK(missing.value(u"state").toString() == u"failed"_s);
    CHECK(missing.value(u"error").toString().contains(u"is not installed in Ollama"_s));

    const QJsonObject vision = fixture.waitFinished(
        fixture.submit(u"text.generate"_s, {{u"prompt"_s, u"Hi"_s}, {u"model"_s, u"vision-only"_s}}));
    CHECK(vision.value(u"state").toString() == u"failed"_s);
    CHECK(vision.value(u"error").toString().contains(u"cannot generate text (it offers: vision)"_s));
    CHECK(fixture.ollama.generateRequests().isEmpty());
}

TEST_CASE("A job fails with a reason when its tool is not running", "[services][ai]")
{
    EngineFixture fixture;
    AiSettings settings = fixture.engine->settings();
    settings.ollamaUrl = QUrl(u"http://127.0.0.1:9"_s);
    REQUIRE(fixture.engine->setSettings(settings));
    const QJsonObject job = fixture.waitFinished(fixture.submit(u"text.generate"_s, {{u"prompt"_s, u"Hi"_s}}));
    CHECK(job.value(u"state").toString() == u"failed"_s);
    CHECK(job.value(u"error").toString().startsWith(u"Ollama is not reachable at http://127.0.0.1:9"_s));
}

TEST_CASE("An error line in Ollama's answer fails the text job", "[services][ai]")
{
    EngineFixture fixture;
    fixture.ollama.answer = {R"({"model":"llama3:latest","response":"Hel","done":false})",
                             R"({"error":"model runner has unexpectedly stopped"})"};
    const QJsonObject job = fixture.waitFinished(fixture.submit(u"text.generate"_s, {{u"prompt"_s, u"Hi"_s}}));
    CHECK(job.value(u"state").toString() == u"failed"_s);
    CHECK(job.value(u"error").toString().startsWith(u"Ollama stopped answering with llama3:latest."_s));
    CHECK(job.value(u"result").toObject().isEmpty());
}

TEST_CASE("Reasoning before </think> is dropped from the answer", "[services][ai]")
{
    CHECK(stripReasoning(u"<think>hmm</think>\n Answer "_s) == u"Answer"_s);
    CHECK(stripReasoning(u"Just an answer"_s) == u"Just an answer"_s);
}

TEST_CASE("image.generate runs the Qwen Image workflow in ComfyUI and copies its image", "[services][ai]")
{
    EngineFixture fixture;
    const QString id = fixture.submit(
        u"image.generate"_s, {{u"prompt"_s, u"A red fox"_s}, {u"width"_s, 1000}, {u"height"_s, 520}, {u"steps"_s, 4}});
    const QJsonObject job = fixture.waitFinished(id);
    INFO(job.value(u"error").toString().toStdString());
    REQUIRE(job.value(u"state").toString() == u"succeeded"_s);
    CHECK(job.value(u"progress").toDouble() == 1.0);

    REQUIRE(fixture.comfy.prompts().size() == 1);
    const QJsonObject workflow = fixture.comfy.prompts().first().second;
    CHECK_FALSE(usesApiNodes(workflow));
    const QJsonObject latent = workflow.value(u"5").toObject().value(u"inputs").toObject();
    CHECK(latent.value(u"width").toInt() == 992);
    CHECK(latent.value(u"height").toInt() == 512);
    const QJsonObject sampler = workflow.value(u"6").toObject().value(u"inputs").toObject();
    CHECK(sampler.value(u"steps").toInt() == 4);
    CHECK(sampler.value(u"sampler_name").toString() == u"euler"_s);
    CHECK(workflow.value(u"4").toObject().value(u"inputs").toObject().value(u"prompt").toString() == u"A red fox"_s);
    CHECK(workflow.value(u"9").toObject().value(u"inputs").toObject().value(u"filename_prefix").toString() ==
          u"mikserstudio/"_s + id);

    const QJsonObject result = job.value(u"result").toObject();
    const QJsonArray outputs = result.value(u"outputs").toArray();
    REQUIRE(outputs.size() == 1);
    const QString path = outputs.first().toObject().value(u"path").toString();
    CHECK(outputs.first().toObject().value(u"kind").toString() == u"image"_s);
    CHECK(path == fixture.dir.filePath(u"cache/%1/image-1.png"_s.arg(id)));
    QFile image(path);
    REQUIRE(image.open(QIODevice::ReadOnly));
    CHECK(image.readAll() == fixture.comfy.image);

    // Without a seed, one is picked, used and returned.
    const qint64 seed = result.value(u"seed").toInteger(-1);
    CHECK(seed >= 0);
    CHECK(sampler.value(u"seed").toInteger() == seed);
    const QJsonObject provenance = result.value(u"provenance").toObject();
    CHECK(provenance.value(u"params").toObject().value(u"seed").toInteger() == seed);
    CHECK(provenance.value(u"tools").toObject().value(u"comfyui").toString() == u"0.9.1"_s);
    CHECK(provenance.value(u"model").toObject().value(u"name").toString() ==
          u"qwen_image_2.1_int8_convrot.safetensors"_s);
    const QJsonObject method = provenance.value(u"method").toObject();
    CHECK(method.value(u"width").toInt() == 992);
    CHECK(method.value(u"seed").toInteger() == seed);
    CHECK(method.value(u"promptId").toString() == u"prompt-1"_s);
}

TEST_CASE("image.generate uses the seed it is given", "[services][ai]")
{
    EngineFixture fixture;
    const QJsonObject job = fixture.waitFinished(
        fixture.submit(u"image.generate"_s, {{u"prompt"_s, u"A fox"_s}, {u"seed"_s, 1234}}));
    REQUIRE(job.value(u"state").toString() == u"succeeded"_s);
    CHECK(job.value(u"result").toObject().value(u"seed").toInteger() == 1234);
    const QJsonObject workflow = fixture.comfy.prompts().first().second;
    CHECK(workflow.value(u"6").toObject().value(u"inputs").toObject().value(u"seed").toInteger() == 1234);
}

TEST_CASE("image.generate copies every image the workflow saved", "[services][ai]")
{
    EngineFixture fixture;
    fixture.comfy.imageCount = 2;
    const QString id = fixture.submit(u"image.generate"_s, {{u"prompt"_s, u"Two foxes"_s}});
    const QJsonObject job = fixture.waitFinished(id);
    REQUIRE(job.value(u"state").toString() == u"succeeded"_s);
    const QJsonArray outputs = job.value(u"result").toObject().value(u"outputs").toArray();
    REQUIRE(outputs.size() == 2);
    CHECK(outputs.at(0).toObject().value(u"path").toString() ==
          fixture.dir.filePath(u"cache/%1/image-1.png"_s.arg(id)));
    CHECK(outputs.at(1).toObject().value(u"path").toString() ==
          fixture.dir.filePath(u"cache/%1/image-2.png"_s.arg(id)));
    CHECK(QFile::exists(outputs.at(1).toObject().value(u"path").toString()));
}

TEST_CASE("Socket events sent before ComfyUI answers the prompt are followed", "[services][ai]")
{
    EngineFixture fixture;
    fixture.comfy.runEarly = true;
    const QJsonObject job = fixture.waitFinished(fixture.submit(u"image.generate"_s, {{u"prompt"_s, u"A fox"_s}}));
    INFO(job.value(u"error").toString().toStdString());
    CHECK(job.value(u"state").toString() == u"succeeded"_s);
}

TEST_CASE("A prompt ComfyUI refuses fails the job with ComfyUI's reason", "[services][ai]")
{
    EngineFixture fixture;
    fixture.comfy.refusePrompts = true;
    const QJsonObject job = fixture.waitFinished(fixture.submit(u"image.generate"_s, {{u"prompt"_s, u"A fox"_s}}));
    CHECK(job.value(u"state").toString() == u"failed"_s);
    CHECK(job.value(u"error").toString().contains(
        u"Prompt outputs failed validation: Value not in list sampler_name: 'euler'"_s));
}

TEST_CASE("A prompt stopped in ComfyUI fails its job", "[services][ai]")
{
    EngineFixture fixture;
    fixture.comfy.holdPrompts = true;
    const QString id = fixture.submit(u"image.generate"_s, {{u"prompt"_s, u"A fox"_s}});
    REQUIRE(waitFor([&] { return fixture.comfy.prompts().size() == 1; }));
    fixture.comfy.interrupt(u"prompt-1"_s);
    const QJsonObject job = fixture.waitFinished(id);
    CHECK(job.value(u"state").toString() == u"failed"_s);
    CHECK(job.value(u"error").toString() == u"The job was stopped in ComfyUI."_s);
    CHECK(fixture.comfy.deleted().isEmpty());
}

TEST_CASE("ComfyUI closing its socket fails the running job", "[services][ai]")
{
    EngineFixture fixture;
    fixture.comfy.holdPrompts = true;
    const QString id = fixture.submit(u"image.generate"_s, {{u"prompt"_s, u"A fox"_s}});
    REQUIRE(waitFor([&] { return fixture.comfy.prompts().size() == 1; }));
    fixture.comfy.closeSockets();
    const QJsonObject job = fixture.waitFinished(id);
    CHECK(job.value(u"state").toString() == u"failed"_s);
    CHECK(job.value(u"error").toString().startsWith(u"ComfyUI closed the connection"_s));
}

TEST_CASE("A ComfyUI error fails the image job with ComfyUI's message", "[services][ai]")
{
    EngineFixture fixture;
    fixture.comfy.holdPrompts = true;
    const QString id = fixture.submit(u"image.generate"_s, {{u"prompt"_s, u"A fox"_s}});
    REQUIRE(waitFor([&] { return fixture.comfy.prompts().size() == 1; }));
    fixture.comfy.fail(u"prompt-1"_s, u"Out of memory "_s);
    const QJsonObject job = fixture.waitFinished(id);
    CHECK(job.value(u"state").toString() == u"failed"_s);
    CHECK(job.value(u"error").toString() == u"ComfyUI stopped with Out of memory in KSampler."_s);
}

TEST_CASE("Availability says what ComfyUI lacks and whether the tools run", "[services][ai]")
{
    EngineFixture fixture;
    fixture.comfy.missingModels = {u"qwen_image_2.1_vae_bf16.safetensors"_s};
    fixture.comfy.missingNodes = {u"QwenImage21Cache"_s};

    const auto find = [&](const QString& id) {
        const auto backends = fixture.engine->backends();
        return *std::ranges::find(backends, id, &AiBackendInfo::id);
    };
    CHECK_FALSE(find(u"comfyui"_s).available);
    CHECK(find(u"comfyui"_s).reason.contains(u"checking"_s));

    fixture.engine->refreshAvailability();
    REQUIRE(waitFor([&] { return !find(u"comfyui"_s).reason.contains(u"checking"_s); }));
    REQUIRE(waitFor([&] { return find(u"ollama"_s).available; }));
    const AiBackendInfo comfy = find(u"comfyui"_s);
    CHECK_FALSE(comfy.available);
    CHECK(comfy.reason.contains(u"qwen_image_2.1_vae_bf16.safetensors"_s));
    CHECK(comfy.reason.contains(u"the node QwenImage21Cache"_s));
    REQUIRE(comfy.operations.size() == 1);
    CHECK(comfy.operations.first().id == u"image.generate"_s);

    // An image job says the same, and ComfyUI gets no prompt.
    const QJsonObject job = fixture.waitFinished(fixture.submit(u"image.generate"_s, {{u"prompt"_s, u"A fox"_s}}));
    CHECK(job.value(u"state").toString() == u"failed"_s);
    CHECK(job.value(u"error").toString().contains(u"ComfyUI lacks"_s));
    CHECK(fixture.comfy.prompts().isEmpty());
}

TEST_CASE("Jobs of one backend run one at a time, other backends side by side", "[services][ai]")
{
    EngineFixture fixture;
    fixture.comfy.holdPrompts = true;
    const QString first = fixture.submit(u"image.generate"_s, {{u"prompt"_s, u"One"_s}});
    const QString second = fixture.submit(u"image.generate"_s, {{u"prompt"_s, u"Two"_s}});
    REQUIRE(waitFor([&] { return fixture.comfy.prompts().size() == 1; }));
    CHECK(fixture.state(first) == u"running"_s);
    CHECK(fixture.state(second) == u"queued"_s);
    CHECK(fixture.engine->activeJobCount() == 2);

    // Ollama is not held up by ComfyUI.
    const QJsonObject text = fixture.waitFinished(fixture.submit(u"text.generate"_s, {{u"prompt"_s, u"Hi"_s}}));
    CHECK(text.value(u"state").toString() == u"succeeded"_s);
    CHECK(fixture.state(first) == u"running"_s);

    fixture.comfy.run(u"prompt-1"_s);
    CHECK(fixture.waitFinished(first).value(u"state").toString() == u"succeeded"_s);
    REQUIRE(waitFor([&] { return fixture.comfy.prompts().size() == 2; }));
    CHECK(fixture.state(second) == u"running"_s);
    fixture.comfy.run(u"prompt-2"_s);
    CHECK(fixture.waitFinished(second).value(u"state").toString() == u"succeeded"_s);
}

TEST_CASE("Canceling takes only the job's own prompt off ComfyUI's queue", "[services][ai]")
{
    EngineFixture fixture;
    fixture.comfy.holdPrompts = true;
    const QString running = fixture.submit(u"image.generate"_s, {{u"prompt"_s, u"One"_s}});
    const QString queued = fixture.submit(u"image.generate"_s, {{u"prompt"_s, u"Two"_s}});
    REQUIRE(waitFor([&] { return fixture.comfy.prompts().size() == 1; }));

    auto state = fixture.engine->cancel(queued);
    REQUIRE(state);
    CHECK(*state == AiJobState::Canceled);
    CHECK(fixture.state(queued) == u"canceled"_s);

    REQUIRE(fixture.engine->cancel(running));
    CHECK(fixture.state(running) == u"canceled"_s);
    REQUIRE(waitFor([&] { return fixture.comfy.deleted() == QStringList{u"prompt-1"_s}; }));
    CHECK_FALSE(fixture.comfy.wasAsked(u"/interrupt"_s));
    CHECK_FALSE(fixture.comfy.wasAsked(u"/free"_s));
    CHECK(fixture.engine->activeJobCount() == 0);

    // A canceled job's late events change nothing, and a finished job cannot be canceled.
    fixture.comfy.run(u"prompt-1"_s);
    tests::spin(200ms);
    CHECK(fixture.state(running) == u"canceled"_s);
    const auto again = fixture.engine->cancel(running);
    REQUIRE_FALSE(again);
    CHECK(again.error().code == core::CommandErrorCode::NotAvailable);
    CHECK_FALSE(fixture.engine->cancel(u"aijob-unknown"_s));
}

TEST_CASE("Closing the engine takes its queued prompts off ComfyUI's queue", "[services][ai]")
{
    EngineFixture fixture;
    fixture.comfy.holdPrompts = true;
    fixture.submit(u"image.generate"_s, {{u"prompt"_s, u"One"_s}});
    REQUIRE(waitFor([&] { return fixture.comfy.prompts().size() == 1; }));
    fixture.engine.reset();
    CHECK(fixture.comfy.deleted() == QStringList{u"prompt-1"_s});
}

TEST_CASE("A new address applies at once and running jobs go on with the old one", "[services][ai]")
{
    EngineFixture fixture;
    fixture.comfy.holdPrompts = true;
    const QString first = fixture.submit(u"image.generate"_s, {{u"prompt"_s, u"One"_s}});
    REQUIRE(waitFor([&] { return fixture.comfy.prompts().size() == 1; }));

    FakeComfyUi other;
    AiSettings settings = fixture.engine->settings();
    settings.comfyuiUrl = other.url();
    REQUIRE(fixture.engine->setSettings(settings));
    const QString second = fixture.submit(u"image.generate"_s, {{u"prompt"_s, u"Two"_s}});

    fixture.comfy.run(u"prompt-1"_s);
    CHECK(fixture.waitFinished(first).value(u"state").toString() == u"succeeded"_s);
    CHECK(fixture.waitFinished(second).value(u"state").toString() == u"succeeded"_s);
    CHECK(fixture.comfy.prompts().size() == 1);
    CHECK(other.prompts().size() == 1);

    // The settings were saved: a new engine starts with them.
    const AiEngine reopened(AiEngineOptions{.cacheDirectory = fixture.dir.filePath(u"cache"_s),
                                            .settingsFile = fixture.dir.filePath(u"ai-settings.json"_s)});
    CHECK(reopened.settings().comfyuiUrl == other.url());
    CHECK(reopened.settings().ollamaUrl == fixture.ollama.url());
}

TEST_CASE("Jobs are refused with params their operation does not take", "[services][ai]")
{
    EngineFixture fixture;
    const auto missingPrompt = fixture.engine->submit(u"image.generate"_s, {}, {}, {});
    REQUIRE_FALSE(missingPrompt);
    CHECK(missingPrompt.error().code == core::CommandErrorCode::InvalidParams);
    const auto tooLarge = fixture.engine->submit(u"image.generate"_s, {{u"prompt"_s, u"x"_s}, {u"width"_s, 4096}}, {},
                                                 {});
    REQUIRE_FALSE(tooLarge);
    CHECK(tooLarge.error().message.contains(u"/width"_s));
    CHECK_FALSE(fixture.engine->submit(u"video.upscale"_s, {}, {}, {}));
    CHECK_FALSE(fixture.engine->submit(u"text.generate"_s, {{u"prompt"_s, u"x"_s}}, u"comfyui"_s, {}));
    CHECK_FALSE(fixture.engine->submit(u"text.generate"_s, {{u"prompt"_s, u"x"_s}}, u"builtin"_s, {}));
    CHECK(fixture.engine->jobsJson().isEmpty());
}

TEST_CASE("AI settings take only http addresses", "[services][ai]")
{
    CHECK(aiSettingsFromJson({{u"comfyuiUrl"_s, u"http://127.0.0.1:8189/"_s}})->comfyuiUrl ==
          QUrl(u"http://127.0.0.1:8189"_s));
    CHECK(aiSettingsFromJson({{u"ollamaUrl"_s, u"ftp://host"_s}}).error().contains(u"Ollama"_s));
    CHECK_FALSE(aiSettingsFromJson({{u"comfyuiUrl"_s, u"not a url"_s}}));
    CHECK(aiSettingsFromJson({}).value() == AiSettings{});
}
