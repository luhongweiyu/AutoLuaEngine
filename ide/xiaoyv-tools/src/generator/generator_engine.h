/**
 * 文件用途：声明自定义代码格式的发现、校验和有执行上限的 Lua/JavaScript 生成入口。
 */
#pragma once

#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

struct lua_State;

namespace xiaoyv::tools {

struct GeneratorFormat {
    QString id;
    QString name;
    QString language;
    QString generatorPath;
};

class GeneratorEngine final : public QObject {
    Q_OBJECT

public:
    explicit GeneratorEngine(QObject* parent = nullptr);

    void reload();
    const QVector<GeneratorFormat>& formats() const;
    const QStringList& loadErrors() const;
    /** 返回 exe 同级的唯一格式目录；内置格式和用户新增格式都存放于此。 */
    QString formatsDirectory() const;
    const GeneratorFormat* find(const QString& id) const;

    /** 执行单个格式；Lua 使用指令/时间预算，JavaScript 使用可中断执行边界。 */
    QString generate(
            const QString& formatId,
            const QVariantMap& context,
            QString* error = nullptr) const;

signals:
    void formatsChanged();

private:
    void loadDirectory(const QString& root, QSet<QString>* ids);
    static QString runLua(const QString& path, const QVariantMap& context, QString* error);
    static QString runJavaScript(const QString& path, const QVariantMap& context, QString* error);
    static void pushLuaValue(::lua_State* state, const QVariant& value);

    QVector<GeneratorFormat> formats_;
    QStringList loadErrors_;
};

} // namespace xiaoyv::tools
