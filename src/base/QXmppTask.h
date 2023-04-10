// SPDX-FileCopyrightText: 2022 Linus Jahn <lnj@kaidan.im>
// SPDX-FileCopyrightText: 2022 Jonah Brüchert <jbb@kaidan.im>
//
// SPDX-License-Identifier: LGPL-2.1-or-later

#ifndef QXMPPTASK_H
#define QXMPPTASK_H

#include "qxmpp_export.h"

#include <functional>
#include <memory>
#include <optional>

#include <QFuture>
#include <QPointer>

template<typename T>
class QXmppPromise;

namespace QXmpp {
    namespace Private {

struct TaskData;

class QXMPP_EXPORT TaskPrivate
{
public:
    TaskPrivate(void (*freeResult)(void *));
    ~TaskPrivate();

    bool isFinished() const;
    void setFinished(bool);
    bool isContextAlive();
    void setContext(QObject *);
    void *result() const;
    void setResult(void *);
    void resetResult() { setResult(nullptr); }
    const std::function<void(TaskPrivate &, void *)> continuation() const;
    void setContinuation(std::function<void(TaskPrivate &, void *)> &&);
    void invokeContinuation(void *result);

private:
    std::shared_ptr<TaskData> d;
};

}  // namespace QXmpp
}  // namespace Private

///
/// Handle for an ongoing operation that finishes in the future.
///
/// Tasks are generated by QXmppPromise and can be handled using QXmppTask::then().
///
/// Unlike QFuture, this is not thread-safe. This avoids the need to do mutex locking at every
/// access though.
///
/// \ingroup Core classes
///
/// \since QXmpp 1.5
///
template<typename T>
class QXmppTask
{
public:
    ~QXmppTask() = default;

    ///
    /// Registers a function that will be called with the result as parameter when the asynchronous
    /// operation finishes.
    ///
    /// If the task is already finished when calling this (and still has a result), the function
    /// will be called immediately.
    ///
    /// If another function was previously registered using .then(), the old function will be
    /// replaced, and only the new one will be called.
    ///
    /// Example usage:
    /// ```
    /// QXmppTask<QString> generateSomething();
    ///
    /// void Manager::generate()
    /// {
    ///     generateSomething().then(this, [](QString &&result) {
    ///         // runs as soon as the result is finished
    ///         qDebug() << "Generating finished:" << result;
    ///     });
    ///
    ///     // The generation could still be in progress here and the lambda might not
    ///     // have been executed yet.
    /// }
    ///
    /// // Manager is derived from QObject.
    /// ```
    ///
    /// \param context QObject used for unregistering the handler function when the object is
    /// deleted. This way your lambda will never be executed after your object has been deleted.
    /// \param continuation A function accepting a result in the form of `T &&`.
    ///
#ifndef QXMPP_DOC
    template<typename Continuation>
#endif
    void then(QObject *context, Continuation continuation)
    {
        if constexpr (!std::is_void_v<T>) {
            static_assert(std::is_invocable_v<Continuation, T &&>, "Function needs to be invocable with T &&.");
        } else {
            static_assert(std::is_invocable_v<Continuation>, "Function needs to be invocable without arguments.");
        }
        using namespace QXmpp::Private;

        if (d.isFinished()) {
            if constexpr (std::is_void_v<T>) {
                continuation();
            } else {
                // when calling then() after finished value could be empty
                if (hasResult()) {
                    continuation(std::move(*reinterpret_cast<T *>(d.result())));
                    d.resetResult();
                }
            }
        } else {
            d.setContext(context);
            d.setContinuation([f = std::forward<Continuation>(continuation)](TaskPrivate &d, void *result) mutable {
                if (d.isContextAlive()) {
                    if constexpr (std::is_void_v<T>) {
                        f();
                    } else {
                        f(std::move(*reinterpret_cast<T *>(result)));
                    }
                }

                // clear continuation to avoid "deadlocks" in case the user captured this QXmppTask
                d.setContinuation({});
            });
        }
    }

    ///
    /// Whether the asynchronous operation is already finished.
    ///
    /// This does not mean that the result is still stored, it might have been taken using
    /// takeResult() or handled using then().
    ///
    [[nodiscard]] bool isFinished() const { return d.isFinished(); }

    ///
    /// Returns whether the task is finished and the value has not been taken yet.
    ///
#ifndef QXMPP_DOC
    template<typename U = T, std::enable_if_t<(!std::is_void_v<U>)> * = nullptr>
#endif
    [[nodiscard]] bool hasResult() const
    {
        return d.result() != nullptr;
    }

    ///
    /// The result of the operation.
    ///
    /// \warning This can only be used once the operation is finished.
    ///
#ifdef QXMPP_DOC
    [[nodiscard]] const T &result() const
#else
    template<typename U = T, std::enable_if_t<(!std::is_void_v<U>)> * = nullptr>
    [[nodiscard]] const U &result() const
#endif
    {
        Q_ASSERT(isFinished());
        Q_ASSERT(hasResult());
        return *reinterpret_cast<U *>(d.result());
    }

    ///
    /// Moves the result of the operation out of the task.
    ///
    /// \warning This can only be used once and only after the operation has finished.
    ///
#ifdef QXMPP_DOC
    [[nodiscard]] T takeResult()
#else
    template<typename U = T, std::enable_if_t<(!std::is_void_v<U>)> * = nullptr>
    [[nodiscard]] U takeResult()
#endif
    {
        Q_ASSERT(isFinished());
        Q_ASSERT(hasResult());
        U result = std::move(*reinterpret_cast<U *>(d.result()));
        d.resetResult();
        return result;
    }

    ///
    /// Converts the Task into a QFuture. Afterwards the QXmppTask object is invalid.
    ///
    QFuture<T> toFuture(QObject *context)
    {
        QFutureInterface<T> interface;

        if constexpr (std::is_same_v<T, void>) {
            then(context, [interface]() mutable {
                interface.reportFinished();
            });
        } else {
            then(context, [interface](T &&val) mutable {
                interface.reportResult(val);
                interface.reportFinished();
            });
        }

        return interface.future();
    }

private:
    friend class QXmppPromise<T>;

    explicit QXmppTask(QXmpp::Private::TaskPrivate data)
        : d(std::move(data))
    {
    }

    QXmpp::Private::TaskPrivate d;
};

#endif  // QXMPPTASK_H
