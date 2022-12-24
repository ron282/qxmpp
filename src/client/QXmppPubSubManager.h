// SPDX-FileCopyrightText: 2020 Linus Jahn <lnj@kaidan.im>
// SPDX-FileCopyrightText: 2022 Melvin Keskin <melvo@olomono.de>
//
// SPDX-License-Identifier: LGPL-2.1-or-later

#ifndef QXMPPPUBSUBMANAGER_H
#define QXMPPPUBSUBMANAGER_H

#include "QXmppClient.h"
#include "QXmppClientExtension.h"
#include "QXmppFutureUtils_p.h"
#include "QXmppMessage.h"
#include "QXmppPubSubIq.h"
#include "QXmppPubSubPublishOptions.h"
#include "QXmppResultSet.h"

#include <QFuture>
#include <QFutureWatcher>

class QXmppPubSubPublishOptions;
class QXmppPubSubSubscribeOptions;

class QXMPP_EXPORT QXmppPubSubManager : public QXmppClientExtension
{
    Q_OBJECT

public:
    ///
    /// Type of PubSub service
    ///
    enum ServiceType {
        PubSubOrPep,  ///< PubSub service or PEP service
        PubSub,       ///< PubSub service only
        Pep           ///< PEP service only
    };

    ///
    /// Pre-defined ID of a PubSub item
    ///
    enum StandardItemId {
        Current  ///< Item of a singleton node (i.e., the node's single item)
    };

    ///
    /// Used to indicate a service type mismatch.
    ///
    struct InvalidServiceType
    {
    };

    template<typename T>
    struct Items
    {
        QVector<T> items;
        std::optional<QXmppResultSetReply> continuation;
    };

    using Result = std::variant<QXmpp::Success, QXmppStanza::Error>;
    using FeaturesResult = std::variant<QVector<QString>, InvalidServiceType, QXmppStanza::Error>;
    using NodesResult = std::variant<QVector<QString>, QXmppStanza::Error>;
    using InstantNodeResult = std::variant<QString, QXmppStanza::Error>;
    template<typename T>
    using ItemResult = std::variant<T, QXmppStanza::Error>;
    template<typename T>
    using ItemsResult = std::variant<Items<T>, QXmppStanza::Error>;
    using ItemIdsResult = std::variant<QVector<QString>, QXmppStanza::Error>;
    using PublishItemResult = std::variant<QString, QXmppStanza::Error>;
    using PublishItemsResult = std::variant<QVector<QString>, QXmppStanza::Error>;
    using SubscriptionsResult = std::variant<QVector<QXmppPubSubSubscription>, QXmppStanza::Error>;
    using AffiliationsResult = std::variant<QVector<QXmppPubSubAffiliation>, QXmppStanza::Error>;
    using OptionsResult = std::variant<QXmppPubSubSubscribeOptions, QXmppStanza::Error>;
    using NodeConfigResult = std::variant<QXmppPubSubNodeConfig, QXmppStanza::Error>;

    QXmppPubSubManager();
    ~QXmppPubSubManager();

    // Generic PubSub (the PubSub service is the given entity)
    QFuture<FeaturesResult> requestFeatures(const QString &serviceJid, ServiceType serviceType = PubSubOrPep);
    QFuture<NodesResult> fetchNodes(const QString &jid);
    QFuture<Result> createNode(const QString &jid, const QString &nodeName);
    QFuture<Result> createNode(const QString &jid, const QString &nodeName, const QXmppPubSubNodeConfig &config);
    QFuture<InstantNodeResult> createInstantNode(const QString &jid);
    QFuture<InstantNodeResult> createInstantNode(const QString &jid, const QXmppPubSubNodeConfig &config);
    QFuture<Result> deleteNode(const QString &jid, const QString &nodeName);
    QFuture<ItemIdsResult> requestItemIds(const QString &serviceJid, const QString &nodeName);
    template<typename T = QXmppPubSubItem>
    QFuture<ItemResult<T>> requestItem(const QString &jid, const QString &nodeName, const QString &itemId);
    template<typename T = QXmppPubSubItem>
    QFuture<ItemResult<T>> requestItem(const QString &jid, const QString &nodeName, StandardItemId itemId);
#if 1
    template<typename T = QXmppPubSubItem>
    QFuture<ItemResult<T>> requestItem(const QString &jid, const QString &nodeName);
#endif
    template<typename T = QXmppPubSubItem>
    QFuture<ItemsResult<T>> requestItems(const QString &jid, const QString &nodeName);
    template<typename T = QXmppPubSubItem>
    QFuture<ItemsResult<T>> requestItems(const QString &jid, const QString &nodeName, const QStringList &itemIds);
    template<typename T>
    QFuture<PublishItemResult> publishItem(const QString &jid, const QString &nodeName, const T &item);
    template<typename T>
    QFuture<PublishItemResult> publishItem(const QString &jid, const QString &nodeName, const T &item, const QXmppPubSubPublishOptions &publishOptions);
    template<typename T>
    QFuture<PublishItemsResult> publishItems(const QString &jid, const QString &nodeName, const QVector<T> &items);
    template<typename T>
    QFuture<PublishItemsResult> publishItems(const QString &jid, const QString &nodeName, const QVector<T> &items, const QXmppPubSubPublishOptions &publishOptions);
    QFuture<Result> retractItem(const QString &jid, const QString &nodeName, const QString &itemId);
    QFuture<Result> retractItem(const QString &jid, const QString &nodeName, StandardItemId itemId);
    QFuture<Result> purgeItems(const QString &jid, const QString &nodeName);
    QFuture<SubscriptionsResult> requestSubscriptions(const QString &jid);
    QFuture<SubscriptionsResult> requestSubscriptions(const QString &jid, const QString &nodeName);
    QFuture<AffiliationsResult> requestNodeAffiliations(const QString &jid, const QString &nodeName);
    QFuture<AffiliationsResult> requestAffiliations(const QString &jid);
    QFuture<AffiliationsResult> requestAffiliations(const QString &jid, const QString &nodeName);
    QFuture<OptionsResult> requestSubscribeOptions(const QString &service, const QString &nodeName);
    QFuture<OptionsResult> requestSubscribeOptions(const QString &service, const QString &nodeName, const QString &subscriberJid);
    QFuture<Result> setSubscribeOptions(const QString &service, const QString &nodeName, const QXmppPubSubSubscribeOptions &options);
    QFuture<Result> setSubscribeOptions(const QString &service, const QString &nodeName, const QXmppPubSubSubscribeOptions &options, const QString &subscriberJid);
    QFuture<NodeConfigResult> requestNodeConfiguration(const QString &service, const QString &nodeName);
    QFuture<Result> configureNode(const QString &service, const QString &nodeName, const QXmppPubSubNodeConfig &config);
    QFuture<Result> cancelNodeConfiguration(const QString &service, const QString &nodeName);
    QFuture<Result> subscribeToNode(const QString &serviceJid, const QString &nodeName, const QString &subscriberJid);
    QFuture<Result> unsubscribeFromNode(const QString &serviceJid, const QString &nodeName, const QString &subscriberJid);

    // PEP-specific (the PubSub service is the current account)
    inline QFuture<FeaturesResult> requestPepFeatures() { return requestFeatures(client()->configuration().jidBare(), Pep); };
    inline QFuture<NodesResult> fetchPepNodes() { return fetchNodes(client()->configuration().jidBare()); };
    inline QFuture<Result> createPepNode(const QString &nodeName) { return createNode(client()->configuration().jidBare(), nodeName); }
    inline QFuture<Result> createPepNode(const QString &nodeName, const QXmppPubSubNodeConfig &config) { return createNode(client()->configuration().jidBare(), nodeName, config); }
    inline QFuture<Result> deletePepNode(const QString &nodeName) { return deleteNode(client()->configuration().jidBare(), nodeName); }
    template<typename T = QXmppPubSubItem>
    inline QFuture<ItemResult<T>> requestPepItem(const QString &nodeName, const QString &itemId) { return requestItem<T>(client()->configuration().jidBare(), nodeName, itemId); }
    template<typename T = QXmppPubSubItem>
    inline QFuture<ItemResult<T>> requestPepItem(const QString &nodeName, StandardItemId itemId) { return requestItem<T>(client()->configuration().jidBare(), nodeName, itemId); }
    template<typename T = QXmppPubSubItem>
    inline QFuture<ItemsResult<T>> requestPepItems(const QString &nodeName) { return requestItems(client()->configuration().jidBare(), nodeName); }
    inline QFuture<ItemIdsResult> requestPepItemIds(const QString &nodeName) { return requestItemIds(client()->configuration().jidBare(), nodeName); }
    template<typename T>
    QFuture<PublishItemResult> publishPepItem(const QString &nodeName, const T &item, const QXmppPubSubPublishOptions &publishOptions);
    template<typename T>
    QFuture<PublishItemResult> publishPepItem(const QString &nodeName, const T &item);
    template<typename T>
    QFuture<PublishItemsResult> publishPepItems(const QString &nodeName, const QVector<T> &items, const QXmppPubSubPublishOptions &publishOptions);
    template<typename T>
    QFuture<PublishItemsResult> publishPepItems(const QString &nodeName, const QVector<T> &items);
    inline QFuture<Result> retractPepItem(const QString &nodeName, const QString &itemId) { return retractItem(client()->configuration().jidBare(), nodeName, itemId); }
    inline QFuture<Result> retractPepItem(const QString &nodeName, StandardItemId itemId) { return retractItem(client()->configuration().jidBare(), nodeName, itemId); }
    inline QFuture<Result> purgePepItems(const QString &nodeName) { return purgeItems(client()->configuration().jidBare(), nodeName); }
    inline QFuture<NodeConfigResult> requestPepNodeConfiguration(const QString &nodeName) { return requestNodeConfiguration(client()->configuration().jidBare(), nodeName); }
    inline QFuture<Result> configurePepNode(const QString &nodeName, const QXmppPubSubNodeConfig &config) { return configureNode(client()->configuration().jidBare(), nodeName, config); }
    inline QFuture<Result> cancelPepNodeConfiguration(const QString &nodeName) { return cancelNodeConfiguration(client()->configuration().jidBare(), nodeName); }

    static QString standardItemIdToString(StandardItemId itemId);

    /// \cond
    QStringList discoveryFeatures() const override;
    bool handleStanza(const QDomElement &element) override;
    /// \endcond

private:
    QFuture<PublishItemResult> publishItem(QXmppPubSubIqBase &&iq);
    QFuture<PublishItemsResult> publishItems(QXmppPubSubIqBase &&iq);
    static QXmppPubSubIq<> requestItemsIq(const QString &jid, const QString &nodeName, const QStringList &itemIds);

    // We may need a d-ptr in the future.
    void *d = nullptr;
};

///
/// Requests a specific item of an entity's node.
///
/// \param jid Jabber ID of the entity hosting the pubsub service. For PEP this
/// should be an account's bare JID
/// \param nodeName the name of the node to query
/// \param itemId the ID of the item to retrieve
/// \return
///
template<typename T>
QFuture<QXmppPubSubManager::ItemResult<T>> QXmppPubSubManager::requestItem(const QString &jid,
                                                                           const QString &nodeName,
                                                                           const QString &itemId)
{
    using namespace QXmpp::Private;
    using Error = QXmppStanza::Error;
    return chainIq(client()->sendIq(requestItemsIq(jid, nodeName, { itemId })), this,
                   [](QXmppPubSubIq<T> &&iq) -> ItemResult<T> {
                       if (!iq.items().isEmpty()) {
                           return iq.items().constFirst();
                       }
                       return Error(Error::Cancel, Error::ItemNotFound, QStringLiteral("No such item has been found."));
                   });
}

#if 1
///
/// Requests a specific item of an entity's node.
///
/// \param jid Jabber ID of the entity hosting the pubsub service. For PEP this
/// should be an account's bare JID
/// \param nodeName the name of the node to query
/// \return
///
template<typename T>
QFuture<QXmppPubSubManager::ItemResult<T>> QXmppPubSubManager::requestItem(const QString &jid,
                                                                           const QString &nodeName)
{
    using namespace QXmpp::Private;
    using Error = QXmppStanza::Error;
    return chainIq(client()->sendIq(requestItemsIq(jid, nodeName, {})), this,
                   [](QXmppPubSubIq<T> &&iq) -> ItemResult<T> {
                       if (!iq.items().isEmpty()) {
                           return iq.items().constFirst();
                       }
                       return Error(Error::Cancel, Error::ItemNotFound, QStringLiteral("No such item has been found."));
                   });
}
#endif

///
/// Requests a specific item of an entity's node.
///
/// \param jid Jabber ID of the entity hosting the pubsub service. For PEP this
/// should be an account's bare JID
/// \param nodeName the name of the node to query
/// \param itemId the ID of the item to retrieve
/// \return
///
template<typename T>
QFuture<QXmppPubSubManager::ItemResult<T>> QXmppPubSubManager::requestItem(const QString &jid,
                                                                           const QString &nodeName,
                                                                           StandardItemId itemId)
{
    return requestItem<T>(jid, nodeName, standardItemIdToString(itemId));
}

///
/// Requests all items of an entity's node.
///
/// \param jid Jabber ID of the entity hosting the pubsub service. For PEP this
/// should be an account's bare JID
/// \param nodeName the name of the node to query
/// \return
///
template<typename T>
QFuture<QXmppPubSubManager::ItemsResult<T>> QXmppPubSubManager::requestItems(const QString &jid,
                                                                             const QString &nodeName)
{
    return requestItems<T>(jid, nodeName, {});
}

///
/// Requests items of an entity's node.
///
/// \param jid Jabber ID of the entity hosting the pubsub service. For PEP this
/// should be an account's bare JID
/// \param nodeName the name of the node to query
/// \param itemIds the IDs of the items to retrieve. If empty, retrieves all
/// items
/// \return
///
template<typename T>
QFuture<QXmppPubSubManager::ItemsResult<T>> QXmppPubSubManager::requestItems(const QString &jid,
                                                                             const QString &nodeName,
                                                                             const QStringList &itemIds)
{
    using namespace QXmpp::Private;
    return chainIq(client()->sendIq(requestItemsIq(jid, nodeName, itemIds)), this,
                   [](QXmppPubSubIq<T> &&iq) -> ItemsResult<T> {
                       return Items<T> {
                           iq.items(),
                           iq.itemsContinuation(),
                       };
                   });
}

///
/// Publishs one item to a pubsub node.
///
/// This is a convenience method equivalent to calling
/// QXmppPubSubManager::publishItem with no publish options.
///
/// \param jid Jabber ID of the entity hosting the pubsub service
/// \param nodeName the name of the node to publish the item to
/// \param item the item to publish
/// \return
///
template<typename T>
QFuture<QXmppPubSubManager::PublishItemResult> QXmppPubSubManager::publishItem(const QString &jid,
                                                                               const QString &nodeName,
                                                                               const T &item)
{
    QXmppPubSubIq<T> request;
    request.setTo(jid);
    request.setItems({ item });
    request.setQueryNode(nodeName);
    return publishItem(std::move(request));
}

///
/// Publishs one item to a pubsub node.
///
/// This is a convenience method equivalent to calling
/// QXmppPubSubManager::publishItem with no publish options.
///
/// \param jid Jabber ID of the entity hosting the pubsub service
/// \param nodeName the name of the node to publish the item to
/// \param item the item to publish
/// \param publishOptions publish-options for the items
/// \return
///
template<typename T>
QFuture<QXmppPubSubManager::PublishItemResult> QXmppPubSubManager::publishItem(const QString &jid,
                                                                               const QString &nodeName,
                                                                               const T &item,
                                                                               const QXmppPubSubPublishOptions &publishOptions)
{
    QXmppPubSubIq<T> request;
    request.setTo(jid);
    request.setItems({ item });
    request.setQueryNode(nodeName);
    request.setDataForm(publishOptions.toDataForm());
    return publishItem(std::move(request));
}

///
/// Publishs items to a pubsub node.
///
/// \param jid Jabber ID of the entity hosting the pubsub service
/// \param nodeName the name of the node to publish the items to
/// \param items the items to publish
/// \return
///
template<typename T>
QFuture<QXmppPubSubManager::PublishItemsResult> QXmppPubSubManager::publishItems(const QString &jid,
                                                                                 const QString &nodeName,
                                                                                 const QVector<T> &items)
{
    QXmppPubSubIq<T> request;
    request.setTo(jid);
    request.setItems(items);
    request.setQueryNode(nodeName);
    return publishItems(std::move(request));
}

///
/// Publishs items to a pubsub node.
///
/// \param jid Jabber ID of the entity hosting the pubsub service
/// \param nodeName the name of the node to publish the items to
/// \param items the items to publish
/// \param publishOptions publish-options for the items
/// \return
///
template<typename T>
QFuture<QXmppPubSubManager::PublishItemsResult> QXmppPubSubManager::publishItems(const QString &jid,
                                                                                 const QString &nodeName,
                                                                                 const QVector<T> &items,
                                                                                 const QXmppPubSubPublishOptions &publishOptions)
{
    QXmppPubSubIq<T> request;
    request.setTo(jid);
    request.setItems(items);
    request.setQueryNode(nodeName);
    request.setDataForm(publishOptions.toDataForm());
    return publishItems(std::move(request));
}

///
/// Publishs one item to a PEP node.
///
/// \param nodeName the name of the PEP node to publish the item to
/// \param item the item to publish
/// \param publishOptions publish-options for fine tuning
/// \return
///
template<typename T>
QFuture<QXmppPubSubManager::PublishItemResult> QXmppPubSubManager::publishPepItem(const QString &nodeName, const T &item, const QXmppPubSubPublishOptions &publishOptions)
{
    return publishItem(client()->configuration().jidBare(), nodeName, item, publishOptions);
}

///
/// Publishs one item to a PEP node.
///
/// \param nodeName the name of the PEP node to publish the item to
/// \param item the item to publish
/// \return
///
template<typename T>
QFuture<QXmppPubSubManager::PublishItemResult> QXmppPubSubManager::publishPepItem(const QString &nodeName, const T &item)
{
    return publishItem(client()->configuration().jidBare(), nodeName, item);
}

///
/// Publishs items to a PEP node.
///
/// \param nodeName the name  of the PEP node to publish the items to
/// \param items the items to publish
/// \param publishOptions publish-options for fine tuning (optional). Pass
/// an empty form to honor the default options of the PEP node
/// \return
///
template<typename T>
QFuture<QXmppPubSubManager::PublishItemsResult> QXmppPubSubManager::publishPepItems(const QString &nodeName, const QVector<T> &items, const QXmppPubSubPublishOptions &publishOptions)
{
    return publishItems(client()->configuration().jidBare(), nodeName, items, publishOptions);
}

///
/// Publishs items to a PEP node.
///
/// \param nodeName the name of the PEP node to publish the items to
/// \param items the items to publish
/// \return
///
template<typename T>
QFuture<QXmppPubSubManager::PublishItemsResult> QXmppPubSubManager::publishPepItems(const QString &nodeName, const QVector<T> &items)
{
    return publishItems(client()->configuration().jidBare(), nodeName, items);
}

#endif  // QXMPPPUBSUBMANAGER_H
