/**
 * 文件用途：实现当前脚本任务的 ALPKG 资源上下文和统一的包内资源读取入口。
 */
#include "package_api.h"

#include <utility>

#include "../../runtime/lua/alpkg_package.h"

namespace xiaoyv::api {
namespace {

// 每个脚本运行时在线程内独立保存自己的包身份，禁止跨任务或包外脚本借用资源。
thread_local std::shared_ptr<AlpkgPackage> gActivePackage;

} // namespace

ScopedAlpkgPackageContext::ScopedAlpkgPackageContext(std::shared_ptr<AlpkgPackage> package)
        : previousPackage_(std::move(gActivePackage)) {
    gActivePackage = std::move(package);
}

ScopedAlpkgPackageContext::~ScopedAlpkgPackageContext() {
    gActivePackage = std::move(previousPackage_);
}

bool readActiveAlpkgResource(
        const std::string& relativePath,
        std::vector<unsigned char>* data,
        std::string* error
) {
    if (data == nullptr) {
        if (error != nullptr) {
            *error = "ALPKG 资源输出缓冲为空";
        }
        return false;
    }
    data->clear();

    if (gActivePackage == nullptr) {
        if (error != nullptr) {
            *error = "当前脚本不是 ALPKG 运行环境";
        }
        return false;
    }

    return gActivePackage->readResourceFile(relativePath, data, error);
}

} // namespace xiaoyv::api
