#pragma once

#include "CallGraphLayoutEngine.hpp"

#include <QAbstractTableModel>

#include <vector>

namespace decompiler {

class IsolatedSubgraphTableModel final : public QAbstractTableModel {
public:
    explicit IsolatedSubgraphTableModel(QObject* parent = nullptr);

    void setComponents(std::vector<CallGraphComponent> components);
    void clear();

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(
        const QModelIndex& index,
        int role = Qt::DisplayRole) const override;
    [[nodiscard]] QVariant headerData(
        int section,
        Qt::Orientation orientation,
        int role = Qt::DisplayRole) const override;
    [[nodiscard]] const CallGraphComponent* componentAt(int row) const noexcept;

private:
    std::vector<CallGraphComponent> components_;
    std::size_t totalNodeCount_ = 0;
};

} // namespace decompiler
