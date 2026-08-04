#include "IsolatedSubgraphTableModel.hpp"

#include <QString>

#include <numeric>

namespace decompiler {

static QString tableAddress(std::uint64_t address) {
    return QStringLiteral("%1").arg(static_cast<qulonglong>(address), 0, 16).toUpper();
}

IsolatedSubgraphTableModel::IsolatedSubgraphTableModel(QObject* parent)
    : QAbstractTableModel(parent) {}

void IsolatedSubgraphTableModel::setComponents(
    std::vector<CallGraphComponent> components) {
    beginResetModel();
    components_ = std::move(components);
    totalNodeCount_ = std::accumulate(
        components_.begin(),
        components_.end(),
        std::size_t {0},
        [](std::size_t total, const auto& component) {
            return total + component.nodeAddresses.size();
        });
    endResetModel();
}

void IsolatedSubgraphTableModel::clear() {
    setComponents({});
}

int IsolatedSubgraphTableModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(components_.size());
}

int IsolatedSubgraphTableModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : 4;
}

QVariant IsolatedSubgraphTableModel::data(const QModelIndex& index, int role) const {
    if(!index.isValid() || index.row() < 0
       || static_cast<std::size_t>(index.row()) >= components_.size()) {
        return {};
    }
    const auto& component = components_[static_cast<std::size_t>(index.row())];
    if(role == Qt::TextAlignmentRole) {
        return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
    }
    if(role == Qt::UserRole) {
        return static_cast<qulonglong>(component.id);
    }
    if(role != Qt::DisplayRole) {
        return {};
    }

    switch(index.column()) {
    case 0:
        return tableAddress(component.representativeAddress);
    case 1:
        return tableAddress(component.secondaryAddress);
    case 2:
        return static_cast<qulonglong>(component.nodeAddresses.size());
    case 3: {
        const auto percent = totalNodeCount_ == 0
                                 ? 0.0
                                 : 100.0 * static_cast<double>(component.nodeAddresses.size())
                                       / static_cast<double>(totalNodeCount_);
        return QStringLiteral("%1%").arg(percent, 0, 'f', 1);
    }
    default:
        return {};
    }
}

QVariant IsolatedSubgraphTableModel::headerData(
    int section,
    Qt::Orientation orientation,
    int role) const {
    if(orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return QAbstractTableModel::headerData(section, orientation, role);
    }
    switch(section) {
    case 0:
        return QStringLiteral("StartEA1");
    case 1:
        return QStringLiteral("StartEA2");
    case 2:
        return QStringLiteral("Count");
    case 3:
        return QStringLiteral("Percent");
    default:
        return {};
    }
}

const CallGraphComponent*
IsolatedSubgraphTableModel::componentAt(int row) const noexcept {
    if(row < 0 || static_cast<std::size_t>(row) >= components_.size()) {
        return nullptr;
    }
    return &components_[static_cast<std::size_t>(row)];
}

} // namespace decompiler
