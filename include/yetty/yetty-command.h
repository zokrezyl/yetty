#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <webgpu/webgpu.h>

namespace yetty {

// Forward declarations
class WebGPUContext;
class Yetty;

//=============================================================================
// YettyCommand - base class for all commands
//=============================================================================
class YettyCommand {
public:
    virtual ~YettyCommand() = default;

    // Execute the command
    // ctx: GPU context for device/queue access
    // engine: Yetty engine (knows current renderable context)
    virtual bool execute(WebGPUContext& ctx, Yetty& engine) = 0;

    // Command type for dispatch
    enum class Type {
        // Resource upload commands
        UploadShader,
        UploadTexture,
        UploadBuffer,

        // Bind commands
        BindShader,
        BindTexture,
        BindBuffer,
        BindFont,  // Shared font from FontManager

        // Draw commands
        BeginRenderPass,
        Draw,
        EndRenderPass,

        // Resource deletion
        DeleteShader,
        DeleteTexture,
        DeleteBuffer,

        // Engine commands (renderable lifecycle)
        CreateRenderable,
        DeleteRenderable,
        StopRenderable,
        StartRenderable,
    };

    virtual Type type() const = 0;

    bool isEngineCommand() const {
        auto t = type();
        return t == Type::CreateRenderable ||
               t == Type::DeleteRenderable ||
               t == Type::StopRenderable ||
               t == Type::StartRenderable;
    }
};

//=============================================================================
// Resource Upload Commands
//=============================================================================

class UploadShaderCmd : public YettyCommand {
public:
    UploadShaderCmd(const std::string& name, const std::string& wgslSource,
                    const std::string& vertexEntry = "vs_main",
                    const std::string& fragmentEntry = "fs_main")
        : name_(name), wgslSource_(wgslSource)
        , vertexEntry_(vertexEntry), fragmentEntry_(fragmentEntry) {}

    bool execute(WebGPUContext& ctx, Yetty& engine) override;
    Type type() const override { return Type::UploadShader; }

    const std::string& name() const { return name_; }

private:
    std::string name_;
    std::string wgslSource_;
    std::string vertexEntry_;
    std::string fragmentEntry_;
};

class UploadTextureCmd : public YettyCommand {
public:
    UploadTextureCmd(const std::string& name, std::vector<uint8_t>&& data,
                     uint32_t width, uint32_t height, WGPUTextureFormat format)
        : name_(name), data_(std::move(data))
        , width_(width), height_(height), format_(format) {}

    bool execute(WebGPUContext& ctx, Yetty& engine) override;
    Type type() const override { return Type::UploadTexture; }

    const std::string& name() const { return name_; }

private:
    std::string name_;
    std::vector<uint8_t> data_;
    uint32_t width_;
    uint32_t height_;
    WGPUTextureFormat format_;
};

class UploadBufferCmd : public YettyCommand {
public:
    UploadBufferCmd(const std::string& name, std::vector<uint8_t>&& data,
                    uint32_t usage)
        : name_(name), data_(std::move(data)), usage_(usage) {}

    bool execute(WebGPUContext& ctx, Yetty& engine) override;
    Type type() const override { return Type::UploadBuffer; }

    const std::string& name() const { return name_; }

private:
    std::string name_;
    std::vector<uint8_t> data_;
    uint32_t usage_;
};

//=============================================================================
// Bind Commands
//=============================================================================

class BindShaderCmd : public YettyCommand {
public:
    explicit BindShaderCmd(const std::string& name) : name_(name) {}

    bool execute(WebGPUContext& ctx, Yetty& engine) override;
    Type type() const override { return Type::BindShader; }

private:
    std::string name_;
};

class BindTextureCmd : public YettyCommand {
public:
    BindTextureCmd(const std::string& name, uint32_t bindingSlot)
        : name_(name), bindingSlot_(bindingSlot) {}

    bool execute(WebGPUContext& ctx, Yetty& engine) override;
    Type type() const override { return Type::BindTexture; }

private:
    std::string name_;
    uint32_t bindingSlot_;
};

class BindBufferCmd : public YettyCommand {
public:
    BindBufferCmd(const std::string& name, uint32_t bindingSlot)
        : name_(name), bindingSlot_(bindingSlot) {}

    bool execute(WebGPUContext& ctx, Yetty& engine) override;
    Type type() const override { return Type::BindBuffer; }

private:
    std::string name_;
    uint32_t bindingSlot_;
};

class BindFontCmd : public YettyCommand {
public:
    // Font descriptor: "family:monospace,style:Regular,size:32"
    BindFontCmd(const std::string& fontDescriptor, uint32_t atlasSlot, uint32_t metadataSlot)
        : fontDescriptor_(fontDescriptor), atlasSlot_(atlasSlot), metadataSlot_(metadataSlot) {}

    bool execute(WebGPUContext& ctx, Yetty& engine) override;
    Type type() const override { return Type::BindFont; }

private:
    std::string fontDescriptor_;
    uint32_t atlasSlot_;
    uint32_t metadataSlot_;
};

//=============================================================================
// Render Pass Commands
//=============================================================================

class BeginRenderPassCmd : public YettyCommand {
public:
    BeginRenderPassCmd(float clearR = 0.1f, float clearG = 0.1f,
                       float clearB = 0.1f, float clearA = 1.0f)
        : clearR_(clearR), clearG_(clearG), clearB_(clearB), clearA_(clearA) {}

    bool execute(WebGPUContext& ctx, Yetty& engine) override;
    Type type() const override { return Type::BeginRenderPass; }

private:
    float clearR_, clearG_, clearB_, clearA_;
};

class DrawCmd : public YettyCommand {
public:
    DrawCmd(uint32_t vertexCount, uint32_t instanceCount = 1,
            uint32_t firstVertex = 0, uint32_t firstInstance = 0)
        : vertexCount_(vertexCount), instanceCount_(instanceCount)
        , firstVertex_(firstVertex), firstInstance_(firstInstance) {}

    bool execute(WebGPUContext& ctx, Yetty& engine) override;
    Type type() const override { return Type::Draw; }

private:
    uint32_t vertexCount_;
    uint32_t instanceCount_;
    uint32_t firstVertex_;
    uint32_t firstInstance_;
};

class EndRenderPassCmd : public YettyCommand {
public:
    EndRenderPassCmd() = default;

    bool execute(WebGPUContext& ctx, Yetty& engine) override;
    Type type() const override { return Type::EndRenderPass; }
};

//=============================================================================
// Resource Deletion Commands
//=============================================================================

class DeleteShaderCmd : public YettyCommand {
public:
    explicit DeleteShaderCmd(const std::string& name) : name_(name) {}

    bool execute(WebGPUContext& ctx, Yetty& engine) override;
    Type type() const override { return Type::DeleteShader; }

private:
    std::string name_;
};

class DeleteTextureCmd : public YettyCommand {
public:
    explicit DeleteTextureCmd(const std::string& name) : name_(name) {}

    bool execute(WebGPUContext& ctx, Yetty& engine) override;
    Type type() const override { return Type::DeleteTexture; }

private:
    std::string name_;
};

class DeleteBufferCmd : public YettyCommand {
public:
    explicit DeleteBufferCmd(const std::string& name) : name_(name) {}

    bool execute(WebGPUContext& ctx, Yetty& engine) override;
    Type type() const override { return Type::DeleteBuffer; }

private:
    std::string name_;
};

//=============================================================================
// Engine Commands (Renderable lifecycle)
//=============================================================================

class CreateRenderableCmd : public YettyCommand {
public:
    CreateRenderableCmd(const std::string& renderableType,
                        const std::string& config = "")
        : renderableType_(renderableType), config_(config) {}

    bool execute(WebGPUContext&, Yetty&) override { return true; }
    Type type() const override { return Type::CreateRenderable; }

    const std::string& renderableType() const { return renderableType_; }
    const std::string& config() const { return config_; }

private:
    std::string renderableType_;
    std::string config_;
};

class DeleteRenderableCmd : public YettyCommand {
public:
    explicit DeleteRenderableCmd(uint32_t renderableId)
        : renderableId_(renderableId) {}

    bool execute(WebGPUContext&, Yetty&) override { return true; }
    Type type() const override { return Type::DeleteRenderable; }

    uint32_t renderableId() const { return renderableId_; }

private:
    uint32_t renderableId_;
};

class StopRenderableCmd : public YettyCommand {
public:
    explicit StopRenderableCmd(uint32_t renderableId)
        : renderableId_(renderableId) {}

    bool execute(WebGPUContext&, Yetty&) override { return true; }
    Type type() const override { return Type::StopRenderable; }

    uint32_t renderableId() const { return renderableId_; }

private:
    uint32_t renderableId_;
};

class StartRenderableCmd : public YettyCommand {
public:
    explicit StartRenderableCmd(uint32_t renderableId)
        : renderableId_(renderableId) {}

    bool execute(WebGPUContext&, Yetty&) override { return true; }
    Type type() const override { return Type::StartRenderable; }

    uint32_t renderableId() const { return renderableId_; }

private:
    uint32_t renderableId_;
};

//=============================================================================
// CommandQueue - container for commands from a Renderable
//=============================================================================
class CommandQueue {
public:
    CommandQueue() = default;
    ~CommandQueue() = default;

    CommandQueue(CommandQueue&&) = default;
    CommandQueue& operator=(CommandQueue&&) = default;
    CommandQueue(const CommandQueue&) = delete;
    CommandQueue& operator=(const CommandQueue&) = delete;

    void push(std::unique_ptr<YettyCommand> cmd) {
        commands_.push_back(std::move(cmd));
    }

    template<typename T, typename... Args>
    void emplace(Args&&... args) {
        commands_.push_back(std::make_unique<T>(std::forward<Args>(args)...));
    }

    std::vector<std::unique_ptr<YettyCommand>>& commands() { return commands_; }
    const std::vector<std::unique_ptr<YettyCommand>>& commands() const { return commands_; }

    void clear() { commands_.clear(); }
    bool empty() const { return commands_.empty(); }
    size_t size() const { return commands_.size(); }
    void reserve(size_t n) { commands_.reserve(n); }

private:
    std::vector<std::unique_ptr<YettyCommand>> commands_;
};

} // namespace yetty
