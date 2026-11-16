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
#include "factory/elementpicturefactory.h"
#include "conductorprofile.h"
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
			//Its ends where they were (the same symbol, refreshed): the
			//wire keeps the way it was drawn
		if (t1->dockConductor() == c->terminal1->dockConductor()
			&& t2->dockConductor() == c->terminal2->dockConductor())
			moved->setProfiles(c->profiles());
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

int TecsideReplace::refreshSymbols(QETProject *project, QStringList *lost)
{
	if (!project || project->isReadOnly())
		return 0;
	project->loadFolios();
	QSet<QString> changed;
	for (const QETProject::OutdatedElement &outdated : project->outdatedElements()) {
		project->updateElement(outdated);
		ElementPictureFactory::instance()->dropCache(outdated.embedded);
		changed.insert(outdated.embedded.toString());
	}
	if (changed.isEmpty())
		return 0;

	QList<Element *> placed;
	for (Diagram *diagram : project->folios())
		if (diagram)
			for (Element *element : diagram->elements())
				if (changed.contains(element->location().toString()))
					placed << element;

	QUndoStack *stack = project->undoStack();
	stack->beginMacro(tr("Refresh the symbols"));
	int count = 0;
	for (Element *element : std::as_const(placed))
		if (replace(element, element->location(), lost))
			++count;
	stack->endMacro();
		//The new wires take their type (and colour) from the sources again
	project->scheduleDerivedPotentialTypes();
	project->setModified(true);
	return count;
}

void TecsideReplace::refreshWithDialog(QETProject *project, QWidget *parent)
{
	if (!project || project->isReadOnly())
		return;
	const QString title = tr("Refresh symbols from the library");
	if (QMessageBox::question(
			parent, title,
			tr("Symbols that changed in the library are updated in this project, "
			   "and each one placed is put down again: its wires, label, location "
			   "and annotation stay, texts you moved by hand go back to their place.\n\n"
			   "Save the project first if you want to be able to go back."))
		!= QMessageBox::Yes)
		return;
	QStringList lost;
	const int count = refreshSymbols(project, &lost);
	QString message = count ? tr("%1 symbols placed again.").arg(count)
							: tr("Every symbol already matches the library.");
	if (!lost.isEmpty())
		message += QStringLiteral("\n\n")
				+ tr("The new symbol has no terminal with the name of these, "
					 "so their wires were removed:\n%1").arg(lost.join(QLatin1Char('\n')));
	QMessageBox::information(parent, title, message);
}
