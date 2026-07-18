/**
 * 文件用途：实现格式目录热加载，并限制自定义生成器的文件大小、执行时间和 Lua 指令数量。
 */
#include "generator/generator_engine.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJSEngine>
#include <QJSValue>
#include <QJsonDocument>
#include <QJsonObject>

#include <atomic>
#include <chrono>
#include <thread>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

namespace xiaoyv::tools {
namespace {

constexpr qint64 kMaximumGeneratorBytes = 1024 * 1024;
constexpr qint64 kMaximumExecutionMs = 2000;
constexpr int kLuaHookInstructionInterval = 10000;
constexpr const char* kLuaTimerRegistryKey = "xiaoyv.generator.timer";

QByteArray readGeneratorFile(const QString& path, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr) *error = file.errorString();
        return {};
    }
    if (file.size() > kMaximumGeneratorBytes) {
        if (error != nullptr) *error = QString::fromUtf8("生成器文件不能超过 1 MiB");
        return {};
    }
    QByteArray source = file.readAll();
    if (source.isEmpty()) {
        if (error != nullptr) *error = QString::fromUtf8("生成器文件为空");
    }
    return source;
}

void luaDeadlineHook(lua_State* state, lua_Debug*) {
    lua_getfield(state, LUA_REGISTRYINDEX, kLuaTimerRegistryKey);
    auto* timer = static_cast<QElapsedTimer*>(lua_touserdata(state, -1));
    lua_pop(state, 1);
    if (timer != nullptr && timer->elapsed() > kMaximumExecutionMs) {
        luaL_error(state, "生成器执行超过 2 秒，已中止");
    }
}

QString luaError(lua_State* state) {
    const char* text = lua_tostring(state, -1);
    return text == nullptr ? QString::fromUtf8("未知 Lua 错误") : QString::fromUtf8(text);
}

} // namespace

GeneratorEngine::GeneratorEngine(QObject* parent)
        : QObject(parent) {
    reload();
}

void GeneratorEngine::reload() {
    formats_.clear();
    loadErrors_.clear();
    QSet<QString> ids;
    const QString root = formatsDirectory();
    if (QDir(root).exists()) {
        // 内置格式在构建时已经复制到这里，用户也可以直接修改或新增同级格式。
        loadDirectory(root, &ids);
    } else {
        loadErrors_.push_back(QString::fromUtf8("格式目录不存在：") + root);
    }
    if (formats_.isEmpty()) loadErrors_.push_back(QString::fromUtf8("没有可用的代码生成格式"));
    std::sort(formats_.begin(), formats_.end(), [](const GeneratorFormat& left, const GeneratorFormat& right) {
        return QString::localeAwareCompare(left.name, right.name) < 0;
    });
    emit formatsChanged();
}

const QVector<GeneratorFormat>& GeneratorEngine::formats() const {
    return formats_;
}

const QStringList& GeneratorEngine::loadErrors() const {
    return loadErrors_;
}

QString GeneratorEngine::formatsDirectory() const {
    // 取色器采用便携式目录布局，格式文件随 exe 放置，便于用户直接编辑和备份。
    const QString path = QDir::cleanPath(
            QCoreApplication::applicationDirPath() + QStringLiteral("/formats"));
    QDir().mkpath(path);
    return path;
}

const GeneratorFormat* GeneratorEngine::find(const QString& id) const {
    for (const GeneratorFormat& format : formats_) {
        if (format.id == id) return &format;
    }
    return nullptr;
}

QString GeneratorEngine::generate(
        const QString& formatId,
        const QVariantMap& context,
        QString* error) const {
    const GeneratorFormat* format = find(formatId);
    if (format == nullptr) {
        if (error != nullptr) *error = QString::fromUtf8("没有找到所选生成格式");
        return {};
    }
    const QString suffix = QFileInfo(format->generatorPath).suffix().toLower();
    if (suffix == QLatin1String("lua")) return runLua(format->generatorPath, context, error);
    if (suffix == QLatin1String("js")) return runJavaScript(format->generatorPath, context, error);
    if (error != nullptr) *error = QString::fromUtf8("生成器只支持 .lua 或 .js");
    return {};
}

void GeneratorEngine::loadDirectory(const QString& root, QSet<QString>* ids) {
    QDir directory(root);
    if (!directory.exists()) return;
    const QFileInfoList children = directory.entryInfoList(
            QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& child : children) {
        const QString metadataPath = child.filePath() + QStringLiteral("/format.json");
        QFile metadataFile(metadataPath);
        if (!metadataFile.open(QIODevice::ReadOnly)) {
            loadErrors_.push_back(QString::fromUtf8("%1：无法读取 format.json")
                    .arg(child.fileName()));
            continue;
        }
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(metadataFile.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            loadErrors_.push_back(QString::fromUtf8("%1：format.json 无效：%2")
                    .arg(child.fileName(), parseError.errorString()));
            continue;
        }
        const QJsonObject object = document.object();
        GeneratorFormat format;
        format.id = object.value(QStringLiteral("id")).toString().trimmed();
        format.name = object.value(QStringLiteral("name")).toString().trimmed();
        format.language = object.value(QStringLiteral("language")).toString().trimmed().toLower();
        const QString generatorName = object.value(QStringLiteral("generator")).toString().trimmed();
        format.generatorPath = child.filePath() + QLatin1Char('/') + generatorName;
        if (format.id.isEmpty() || format.name.isEmpty() || format.language.isEmpty()
                || generatorName.isEmpty() || !QFileInfo::exists(format.generatorPath)) {
            loadErrors_.push_back(QString::fromUtf8("%1：格式配置缺少必要字段或生成器文件")
                    .arg(child.fileName()));
            continue;
        }
        const QString suffix = QFileInfo(format.generatorPath).suffix().toLower();
        if (suffix != QLatin1String("lua") && suffix != QLatin1String("js")) {
            loadErrors_.push_back(QString::fromUtf8("%1：生成器只支持 .lua 或 .js")
                    .arg(child.fileName()));
            continue;
        }
        if (ids->contains(format.id)) {
            loadErrors_.push_back(QString::fromUtf8("%1：格式 ID %2 已存在，已跳过")
                    .arg(child.fileName(), format.id));
            continue;
        }
        ids->insert(format.id);
        formats_.push_back(std::move(format));
    }
}

QString GeneratorEngine::runLua(
        const QString& path,
        const QVariantMap& context,
        QString* error) {
    QByteArray source = readGeneratorFile(path, error);
    if (source.isEmpty()) return {};

    lua_State* state = luaL_newstate();
    if (state == nullptr) {
        if (error != nullptr) *error = QString::fromUtf8("无法创建 Lua 生成器运行时");
        return {};
    }
    luaL_openlibs(state);
    QElapsedTimer timer;
    timer.start();
    lua_pushlightuserdata(state, &timer);
    lua_setfield(state, LUA_REGISTRYINDEX, kLuaTimerRegistryKey);
    lua_sethook(state, luaDeadlineHook, LUA_MASKCOUNT, kLuaHookInstructionInterval);

    const QByteArray fileName = QFile::encodeName(path);
    if (luaL_loadbufferx(state, source.constData(), source.size(), fileName.constData(), "t") != LUA_OK
            || lua_pcall(state, 0, 0, 0) != LUA_OK) {
        if (error != nullptr) *error = luaError(state);
        lua_close(state);
        return {};
    }
    lua_getglobal(state, "generate");
    if (!lua_isfunction(state, -1)) {
        if (error != nullptr) *error = QString::fromUtf8("Lua 生成器必须定义 generate(context)");
        lua_close(state);
        return {};
    }
    pushLuaValue(state, context);
    if (lua_pcall(state, 1, 1, 0) != LUA_OK) {
        if (error != nullptr) *error = luaError(state);
        lua_close(state);
        return {};
    }
    if (!lua_isstring(state, -1)) {
        if (error != nullptr) *error = QString::fromUtf8("Lua generate(context) 必须返回字符串");
        lua_close(state);
        return {};
    }
    const QString result = QString::fromUtf8(lua_tostring(state, -1));
    lua_close(state);
    if (error != nullptr) error->clear();
    return result;
}

QString GeneratorEngine::runJavaScript(
        const QString& path,
        const QVariantMap& context,
        QString* error) {
    QByteArray source = readGeneratorFile(path, error);
    if (source.isEmpty()) return {};

    QJSEngine engine;
    std::atomic_bool finished = false;
    std::jthread watchdog([&engine, &finished](std::stop_token stopToken) {
        const auto deadline = std::chrono::steady_clock::now()
                + std::chrono::milliseconds(kMaximumExecutionMs);
        while (!stopToken.stop_requested() && !finished.load(std::memory_order_acquire)) {
            if (std::chrono::steady_clock::now() >= deadline) {
                engine.setInterrupted(true);
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });

    const QJSValue evaluated = engine.evaluate(QString::fromUtf8(source), path);
    if (evaluated.isError()) {
        finished.store(true, std::memory_order_release);
        watchdog.request_stop();
        if (error != nullptr) *error = evaluated.toString();
        return {};
    }
    QJSValue generate = engine.globalObject().property(QStringLiteral("generate"));
    if (!generate.isCallable()) {
        finished.store(true, std::memory_order_release);
        watchdog.request_stop();
        if (error != nullptr) *error = QString::fromUtf8("JavaScript 生成器必须定义 generate(context)");
        return {};
    }
    const QJSValue value = generate.call({engine.toScriptValue(context)});
    finished.store(true, std::memory_order_release);
    watchdog.request_stop();
    if (engine.isInterrupted()) {
        if (error != nullptr) *error = QString::fromUtf8("生成器执行超过 2 秒，已中止");
        return {};
    }
    if (value.isError()) {
        if (error != nullptr) *error = value.toString();
        return {};
    }
    if (!value.isString()) {
        if (error != nullptr) *error = QString::fromUtf8("JavaScript generate(context) 必须返回字符串");
        return {};
    }
    if (error != nullptr) error->clear();
    return value.toString();
}

void GeneratorEngine::pushLuaValue(lua_State* state, const QVariant& value) {
    if (!value.isValid() || value.isNull()) {
        lua_pushnil(state);
        return;
    }
    switch (value.typeId()) {
        case QMetaType::Bool:
            lua_pushboolean(state, value.toBool());
            return;
        case QMetaType::Int:
        case QMetaType::UInt:
        case QMetaType::LongLong:
        case QMetaType::ULongLong:
            lua_pushinteger(state, static_cast<lua_Integer>(value.toLongLong()));
            return;
        case QMetaType::Double:
        case QMetaType::Float:
            lua_pushnumber(state, static_cast<lua_Number>(value.toDouble()));
            return;
        case QMetaType::QString: {
            const QByteArray text = value.toString().toUtf8();
            lua_pushlstring(state, text.constData(), text.size());
            return;
        }
        case QMetaType::QVariantList: {
            const QVariantList list = value.toList();
            lua_createtable(state, list.size(), 0);
            for (int index = 0; index < list.size(); ++index) {
                pushLuaValue(state, list[index]);
                lua_rawseti(state, -2, index + 1);
            }
            return;
        }
        case QMetaType::QVariantMap: {
            const QVariantMap map = value.toMap();
            lua_createtable(state, 0, map.size());
            for (auto iterator = map.constBegin(); iterator != map.constEnd(); ++iterator) {
                const QByteArray key = iterator.key().toUtf8();
                pushLuaValue(state, iterator.value());
                lua_setfield(state, -2, key.constData());
            }
            return;
        }
        default: {
            const QByteArray text = value.toString().toUtf8();
            lua_pushlstring(state, text.constData(), text.size());
            return;
        }
    }
}

} // namespace xiaoyv::tools
