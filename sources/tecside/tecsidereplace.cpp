/*
	Copyright 2006-2026 The QElectroTech Team
	This file is part of QElectroTech.

	QElectroTech is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.

	QElectroTech is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with QElectroTech.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "tecside/tecsidereplace.h"
#include "ElementsCollection/elementslocation.h"
#include "diagram.h"
#include "diagramcontent.h"
#include "elementdialog.h"
#include "factory/elementfactory.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"
#include "qetproject.h"
#include "undocommand/addgraphicsobjectcommand.h"
#include "undocommand/deleteqgraphicsitemcommand.h"
#include <QCoreApplication>
#include <QMessageBox>
#include <QUndoStack>

namespace {
	QString tr(const char *text)
	{
		return QCoreApplication::translate("TecsideReplace", text);
	}

	Terminal *namesake(const Element *element, const Terminal *terminal)
	{
		const QString name = terminal->name().trimmed();
		for (Terminal *t : element->terminals())
			if (!name.isEmpty() && t->name().trimmed().compare(name, Qt::CaseInsensitive) == 0)
				return t;
		return nullptr;
	}
}

Element *TecsideReplace::replace(Element *old, const ElementsLocation &location,
								 QStringList *lost)
{
	Diagram *diagram = old ? old->diagram() : nullptr;
	if (!diagram || !diagram->project())
		return nullptr;
	QETProject *project = diagram->project();

	ElementsLocation source = location;
	ElementsLocation imported = project->importElement(source);
	if (!imported.exist())
		return nullptr;
	int state = 0;
	Element *element = ElementFactory::Instance()->createElement(imported, nullptr, &state);
	if (state || !element) {
		delete element;
		return nullptr;
	}

		//What the new symbol does not say itself comes from the old one:
		//label, location, notes; manufacturer and model are the new one's
	DiagramContext info = element->elementInformations();
	const DiagramContext old_info = old->elementInformations();
	for (const QString &key : old_info.keys())
		if (info.value(key).toString().isEmpty())
			info.addValue(key, old_info.value(key));
	element->setElementInformations(info);
	element->setRotation(old->rotation());
	element->setPos(old->pos());
	diagram->addItem(element);

	auto *macro = new QUndoCommand(tr("Replace %1 by %2").arg(old->name(), element->name()));
	new AddGraphicsObjectCommand(element, diagram, old->pos(), macro);

	DiagramContent content;
	content.m_elements << old;
	for (Conductor *c : old->conductors())
		if (!content.m_other_conductors.contains(c))
			content.m_other_conductors << c;
	new DeleteQGraphicsItemCommand(diagram, content, macro);

	for (Conductor *c : std::as_const(content.m_other_conductors)) {
		Terminal *t1 = c->terminal1->parentElement() == old
				? namesake(element, c->terminal1) : c->terminal1;
		Terminal *t2 = c->terminal2->parentElement() == old
				? namesake(element, c->terminal2) : c->terminal2;
		if (!t1 || !t2) {
			if (lost) {
				const Terminal *at_old = t1 ? c->terminal2 : c->terminal1;
				*lost << at_old->name();
			}
			continue;
		}
		auto *moved = new Conductor(t1, t2);
		moved->setProperties(c->properties());
		new AddGraphicsObjectCommand(moved, diagram, QPointF(), macro);
	}

	project->undoStack()->push(macro);
	return element;
}

void TecsideReplace::replaceWithDialog(Element *old, QWidget *parent)
{
	if (!old || !old->diagram() || old->diagram()->isReadOnly())
		return;
	const ElementsLocation location = ElementDialog::getOpenElementLocation(parent);
	if (!location.isElement())
		return;
	QStringList lost;
	if (!replace(old, location, &lost)) {
		QMessageBox::warning(parent, tr("Replace with"),
							 tr("This symbol could not be loaded."));
		return;
	}
	if (!lost.isEmpty())
		QMessageBox::information(
			parent, tr("Replace with"),
			tr("The new symbol has no terminal with the name of these, "
			   "so their wires were removed:\n%1").arg(lost.join(QLatin1Char('\n'))));
}
