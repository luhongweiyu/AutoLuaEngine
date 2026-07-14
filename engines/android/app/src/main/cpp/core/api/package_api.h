/**
 * 文件用途：声明当前脚本任务的 ALPKG 资源上下文，供 Lua、JS、Go 和插件复用。
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

class AlpkgPackage;

namespace xiaoyv::api {

/**
 * 在当前脚本 native 工作线程绑定一个 ALPKG 包。
 *
 * 包资源访问必须依赖“当前任务”的包身份，不能由脚本传入任意 APK 路径。该作用域使用
 * 线程局部状态由主任务、Lua 子线程和直接 Java 回调分别绑定；析构时恢复外层上下文，
 * 避免普通 Lua、JS 或 Go 任务继承前一个 ALPKG 任务的读取权限。
 */
class ScopedAlpkgPackageContext {
public:
    explicit ScopedAlpkgPackageContext(std::shared_ptr<AlpkgPackage> package);
    ~ScopedAlpkgPackageContext();

    ScopedAlpkgPackageContext(const ScopedAlpkgPackageContext&) = delete;
    ScopedAlpkgPackageContext& operator=(const ScopedAlpkgPackageContext&) = delete;

private:
    std::shared_ptr<AlpkgPackage> previousPackage_;
};

/**
 * 从当前任务绑定的 ALPKG 中读取一个 manifest resource 条目。
 *
 * 这是语言无关的真实实现：Lua、JS、Go 绑定均应通过 system_c_api 调用它，而不是
 * 自行解析 ZIP 或在各运行时重复实现路径校验。成功时 data 写入完整二进制内容；失败
 * 时返回 false 并写入可直接展示给脚本用户的中文错误。
 */
bool readActiveAlpkgResource(
        const std::string& relativePath,
        std::vector<unsigned char>* data,
        std::string* error
);

} // namespace xiaoyv::api
