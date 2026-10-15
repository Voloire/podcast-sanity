#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>

#include "pages/pages.h"
#include "theme.h"
#include "widgets.h"

namespace gui {

Card* Page::add_card(const QString& title, const QString& subtitle) {
    auto* card = new Card(title, subtitle);
    column_->addWidget(card);
    return card;
}

Page::Page(const QString& title, const QString& subtitle, const QString& badge, QWidget* parent)
    : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    scroll_ = new QScrollArea(this);
    scroll_->setWidgetResizable(true);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* host = new QWidget();
    column_ = new QVBoxLayout(host);
    column_->setContentsMargins(24, 20, 24, 28);
    column_->setSpacing(16);

    // Title block.
    auto* head = new QWidget(host);
    auto* hl = new QHBoxLayout(head);
    hl->setContentsMargins(0, 0, 0, 4);
    hl->setSpacing(12);
    auto* titles = new QVBoxLayout();
    titles->setSpacing(2);
    auto* t = new QLabel(title, head);
    t->setObjectName("PageTitle");
    titles->addWidget(t);
    if (!subtitle.isEmpty()) {
        auto* s = new QLabel(subtitle, head);
        s->setObjectName("PageSubtitle");
        s->setWordWrap(true);
        titles->addWidget(s);
    }
    hl->addLayout(titles, 1);
    badge_ = new Chip(head);
    badge_->set_state(State::Idle, badge);
    hl->addWidget(badge_, 0, Qt::AlignTop);
    column_->addWidget(head);

    column_->addStretch(1);
    scroll_->setWidget(host);
    outer->addWidget(scroll_);
}

void Page::set_badge(const QString& text, State state) { badge_->set_state(state, text); }

}  // namespace gui
