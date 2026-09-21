/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/ErrorMessagesLib.h>
#include <tsre/ErrorMessage.h>
#include <routeEditor/ErrorMessagesWindow.h>

QVector<ErrorMessage*> ErrorMessagesLib::ErrorMessages;
ErrorMessagesWindow* ErrorMessagesLib::Window = NULL;

namespace {
struct ShowRequest {
    ErrorMessage *message;
    std::function<bool()> isCurrent;
};
QVector<ShowRequest> showRequests;
}

ErrorMessagesWindow* ErrorMessagesLib::GetWindow(QWidget *w){
    if(Window == NULL) {
        Window = new ErrorMessagesWindow(w);
        QObject::connect(Window, &QObject::destroyed, [] { Window = nullptr; });
    }
    return Window;
}

bool ErrorMessagesLib::ShowMessage(ErrorMessage *message, QWidget *parent) {
    if (!message || !ErrorMessages.contains(message)) return false;
    return GetWindow(parent)->showMessage(message);
}

void ErrorMessagesLib::RequestShowMessage(ErrorMessage *message, std::function<bool()> isCurrent) {
    if (!message || !ErrorMessages.contains(message)) return;
    for (const auto &request : showRequests)
        if (request.message == message) return;
    showRequests.push_back({message, std::move(isCurrent)});
}

bool ErrorMessagesLib::ShowRequestedMessage(QWidget *parent) {
    // One opening per load, selecting the first still-current request. All other
    // messages remain in the list, without repeated focus stealing.
    const auto requests = std::move(showRequests);
    showRequests.clear();
    for (const auto &request : requests)
        if ((!request.isCurrent || request.isCurrent()) && ShowMessage(request.message, parent))
            return true;
    return false;
}

QString ErrorMessagesLib::PushErrorMessage(ErrorMessage* e){
    QString reply = "";
    ErrorMessages.push_back(e);
    
    if(Window != NULL)
        if(Window->isVisible())
            Window->refreshErrorList();
    
    return reply;
}

ErrorMessagesLib::ErrorMessagesLib() {
}

ErrorMessagesLib::~ErrorMessagesLib() {
}

