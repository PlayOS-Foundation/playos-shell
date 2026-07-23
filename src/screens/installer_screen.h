#pragma once

#include "../core/screen.h"
#include "../core/app_context.h"
#include <string>
#include <sys/types.h>

class InstallerScreen : public IScreen {
public:
    explicit InstallerScreen(AppContext& ctx);

    void OnEnter() override;
    void Update(float dt) override;
    void Draw(int W, int H) override;

private:
    AppContext& m_ctx;
    int m_selected = 1;        // 0 = Confirm, 1 = Cancel (default Cancel for safety)
    bool m_installing = false;
    float m_installTimer = 0.0f;
    float m_completeTimer = 0.0f;
    int m_rebootSelected = 0;  // 0 = Reboot Now, 1 = Stay in Shell

    // Install pipeline state
    enum class Step {
        FindingImage,
        WritingImage,
        RelocatingGPT,
        ResizingPartition,
        ResizingFS,
        Rebooting,
        Done,
        Failed
    };
    Step m_step = Step::FindingImage;
    float m_stepProgress = 0.0f;  // 0.0 - 1.0 within current step
    std::string m_errorMsg;

    // dd progress parsing
    pid_t m_childPid = -1;
    long long m_bytesWritten = 0;
    long long m_totalBytes = 0;

    std::string m_diskName;
    std::string m_diskPath;
    std::string m_diskSize;
    std::string m_imagePath;

    void DetectDisk();
    bool FindImage();
    void StartInstall();
    void RunWriteStep();
    void RunPartitionSteps();
    void ParseDDProgress();
    void CleanupChild();
    static constexpr const char* kProgressFile = "/run/playos/dd-progress";
};
