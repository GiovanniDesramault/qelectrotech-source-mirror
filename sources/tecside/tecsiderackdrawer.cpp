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
#include "tecside/tecsiderackdrawer.h"

#include "ElementsCollection/elementslocation.h"
#include "bordertitleblock.h"
#include "diagram.h"
#include "factory/elementfactory.h"
#include "qetapp.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/independenttextitem.h"
#include "qetinformation.h"
#include "qetproject.h"
#include "tecside/tecsidelocations.h"
#include "tecside/tecsiderack.h"
#include "undocommand/addgraphicsobjectcommand.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QInputDialog>
#include <QMessageBox>
#include <QRegularExpression>
#include <QUndoStack>

#include <algorithm>

namespace
{
	const QString rack_folder = QStringLiteral("av/rack/");

		//A device of the place, as the schematic has it
	struct Source
	{
		Element *element = nullptr;
		int folio = 0;
		QString front;	///< collection path of its front, empty if none
	};

		//"label (name)", the name alone without a label
	QString labelOf(const Element *element)
	{
		QString label = element->actualLabel();
		if (label.isEmpty())
			label = element->elementInformations()
					.value(QETInformation::ELMT_LABEL).toString();
		return label;
	}

	bool isAmplifier(const Element *element)
	{
		const QString category = QStringLiteral("category");
		if (element->elementInformations().value(category).toString()
				== QLatin1String("amplifier"))
			return true;
		DiagramContext definition;
		definition.fromXml(element->location().xml()
						   .firstChildElement(QStringLiteral("elementInformations")),
						   QStringLiteral("elementInformation"));
		return definition.value(category).toString() == QLatin1String("amplifier");
	}

		//A front's size and units, from its definition: build_rack.py
		//draws from (0, 0) and gives the definition a 10 px margin, and
		//writes "2U full width" or "desktop unit ..." in its description
	TecsideRack::Device frontOf(const QString &path)
	{
		TecsideRack::Device device;
		const QDomElement xml = ElementsLocation(path).xml();
		device.size = QSizeF(xml.attribute(QStringLiteral("width")).toDouble() - 20,
							 xml.attribute(QStringLiteral("height")).toDouble() - 20);
		DiagramContext info;
		info.fromXml(xml.firstChildElement(QStringLiteral("elementInformations")),
					 QStringLiteral("elementInformation"));
		static const QRegularExpression units(QStringLiteral("^\\s*(\\d+)\\s*U\\b"));
		const QRegularExpressionMatch match =
				units.match(info.value(QStringLiteral("description")).toString());
		if (match.hasMatch())
			device.units = match.captured(1).toInt();
		return device;
	}

		//The element of collection @p path, in the embedded collection of
		//@p project; one of the same name already there is taken as it
		//is, never the import dialog
	Element *build(QETProject *project, const QString &path)
	{
		ElementsLocation location(path);
		if (!location.isElement() || !location.exist())
			return nullptr;
		ElementsLocation imported(QStringLiteral("import/") + location.collectionPath(false),
								  project);
		if (!imported.exist())
			imported = project->importElement(location);
		if (!imported.exist())
			return nullptr;
		int state = 0;
		Element *element = ElementFactory::Instance()->createElement(imported, nullptr, &state);
		if (state) {
			delete element;
			return nullptr;
		}
		return element;
	}

	void setInfo(Element *element, const QString &label, const QString &location)
	{
		DiagramContext info = element->elementInformations();
		info.addValue(QETInformation::ELMT_LABEL, label);
		info.addValue(QETInformation::ELMT_LOCATION, location);
		element->setElementInformations(info);
	}

		//Make the new folio @p diagram @p size big from @p origin of its
		//drawing area, under @p title
	void setUpFolio(Diagram *diagram, const QString &title, const QSizeF &size,
					QPointF *origin)
	{
		BorderTitleBlock &border = diagram->border_and_titleblock;
		TitleBlockProperties title_block = border.exportTitleBlock();
		title_block.title = title;
		border.importTitleBlock(title_block);

		BorderProperties border_properties = border.exportBorder();
		const QPointF margin(40, 50);
		auto free_size = [&border]() {
			QSizeF free = border.insideBorderRect().size();
			if (border.titleBlockIsDisplayed()) {
				if (border.titleBlockEdge() == Qt::BottomEdge)
					free.rheight() -= border.titleBlockRect().height();
				else
					free.rwidth() -= border.titleBlockRect().width();
			}
			return free;
		};
		const QSizeF needed = size + QSizeF(2 * margin.x(), 2 * margin.y());
		for (int i = 0 ; i < 200 && free_size().height() < needed.height() ; ++i) {
			++border_properties.rows_count;
			border.importBorder(border_properties);
		}
		for (int i = 0 ; i < 200 && free_size().width() < needed.width() ; ++i) {
			++border_properties.columns_count;
			border.importBorder(border_properties);
		}
		diagram->queueValuesStore();
		*origin = Diagram::snapToGrid(border.insideBorderRect().topLeft() + margin);
	}
}

/**
	@return the rack symbols under av/rack of the collection in
	@p collection_dir (the company's), their paths starting with
	@p protocol: the fronts devices/.../<id>_front.elmt by the id of their
	device, the frames frames/rack_frame_<n>u.elmt, the blind panels
	blind_panels/blind_<n>u.elmt and the shelves shelves/shelf_<n>u.elmt
	by their units
*/
TecsideRackDrawer::Symbols TecsideRackDrawer::findSymbols(const QString &collection_dir,
														  const QString &protocol)
{
	Symbols symbols;
	const QDir root(collection_dir);
	const QString rack = root.filePath(rack_folder);
	if (!QFileInfo(rack).isDir())
		return symbols;
	static const QRegularExpression sized(
		QStringLiteral("^(frames/rack_frame|blind_panels/blind|shelves/shelf)_(\\d+)u\\.elmt$"));
	QDirIterator it(rack, {QStringLiteral("*.elmt")}, QDir::Files,
					QDirIterator::Subdirectories);
	while (it.hasNext()) {
		const QString file = it.next();
		const QString relative = QDir(rack).relativeFilePath(file);
		const QString path = protocol + rack_folder + relative;
		const QString base = QFileInfo(file).completeBaseName();
		if (relative.startsWith(QLatin1String("devices/"))
			&& base.endsWith(QLatin1String("_front"))) {
			symbols.fronts.insert(base.chopped(6), path);
			continue;
		}
		const QRegularExpressionMatch match = sized.match(relative);
		if (!match.hasMatch())
			continue;
		const int units = match.captured(2).toInt();
		const QString kind = match.captured(1);
		if (kind.startsWith(QLatin1String("frames")))
			symbols.frames.insert(units, path);
		else if (kind.startsWith(QLatin1String("blind")))
			symbols.blinds.insert(units, path);
		else
			symbols.shelves.insert(units, path);
	}
	return symbols;
}

/**
	@return the id of the device @p element draws: the name of its file,
	"dap_ta_4_450" for .../dap_ta_4_450.elmt, the front of which is
	dap_ta_4_450_front.elmt
*/
QString TecsideRackDrawer::deviceId(const Element *element)
{
	return QFileInfo(element->location().fileName()).completeBaseName();
}

/**
	@return true when @p element is itself a rack symbol (from av/rack, or
	a front): an earlier rack is not counted in the next
*/
bool TecsideRackDrawer::isRackSymbol(const Element *element)
{
	return element->location().collectionPath(false).contains(rack_folder)
			|| deviceId(element).endsWith(QLatin1String("_front"));
}

/**
	Draw the rack of the place numbered @p number of @p project on a new
	folio at its end, from @p symbols: every element of the project with
	that location that has a front, in the order of the schematic (folio,
	then top to bottom, then left to right), laid out by
	TecsideRack::layout(); beside it, the ones without a front. Each front
	takes the label and location of its device. The folio and all on it
	are one undo step.
*/
Diagram *TecsideRackDrawer::draw(QETProject *project, const QString &number,
								 const Symbols &symbols, QString *error)
{
	auto fail = [error](const QString &message) -> Diagram * {
		if (error)
			*error = message;
		return nullptr;
	};
	if (!project || project->isReadOnly())
		return fail(QCoreApplication::translate("TecsideRack", "The project cannot be changed."));
	if (!symbols.isComplete())
		return fail(QCoreApplication::translate("TecsideRack", "The company collection has no rack symbols: av/rack needs "
					   "its frames, a 1U blind panel and a shelf. Generate them "
					   "with build_rack.py first."));

	QList<Source> sources;
	const QList<Diagram *> folios = project->folios();
	for (int f = 0 ; f < folios.size() ; ++f) {
		Diagram *folio = folios.at(f);
		project->buildFolioToShow(folio);
		folio->ensureLoaded();
		for (Element *element : folio->elements()) {
			if (element->elementInformations().value(QETInformation::ELMT_LOCATION)
					.toString() != number || isRackSymbol(element))
				continue;
			sources.append({element, f, symbols.fronts.value(deviceId(element))});
		}
	}
	if (sources.isEmpty())
		return fail(QCoreApplication::translate("TecsideRack", "No element has the location %1.").arg(number));
	std::stable_sort(sources.begin(), sources.end(), [](const Source &a, const Source &b) {
		const QPointF pa = a.element->scenePos(), pb = b.element->scenePos();
		if (a.folio != b.folio) return a.folio < b.folio;
		if (pa.y() != pb.y()) return pa.y() < pb.y();
		return pa.x() < pb.x();
	});

	QList<TecsideRack::Device> devices;
	for (const Source &source : sources) {
		TecsideRack::Device device;
		if (!source.front.isEmpty())
			device = frontOf(source.front);
		device.name = labelOf(source.element);
		device.hasFront = !source.front.isEmpty();
		device.isAmp = isAmplifier(source.element);
		devices << device;
	}
	const TecsideRack::Layout layout = TecsideRack::layout(
		devices, symbols.frames.keys(), symbols.shelves.keys());

	const TecsideLocations &locations = project->tecsideLocations();
	const QString place = locations.label({number, locations.nameOf(number)});
	int highest = 0;
	for (int units : layout.frames)
		highest = std::max(highest, units);
	const QSizeF size(layout.frames.size() * TecsideRack::framePitch + 240,
					  highest * TecsideRack::unit + 40);
		//One undo step: addNewDiagram() pushes its own command
	QUndoStack *stack = project->undoStack();
	stack->beginMacro(QCoreApplication::translate("TecsideRack", "Draw the rack of %1")
					  .arg(place));
	Diagram *diagram = project->addNewDiagram();
	if (!diagram) {
		stack->endMacro();
		return fail(QCoreApplication::translate("TecsideRack", "The project cannot be changed."));
	}
	QPointF origin;
	setUpFolio(diagram, QCoreApplication::translate("TecsideRack", "Rack %1").arg(place),
			   size, &origin);
	auto *macro = new QUndoCommand();
	auto place_element = [&](const QString &path, const QPointF &pos) -> Element * {
		Element *element = build(project, path);
		if (!element)
			return nullptr;
		element->setPos(origin + pos);
		new AddGraphicsObjectCommand(element, diagram, origin + pos, macro);
		return element;
	};

	for (const TecsideRack::Placement &placement : layout.placements) {
		switch (placement.kind) {
			case TecsideRack::Placement::Frame: {
				Element *frame = place_element(symbols.frames.value(placement.units),
											   placement.pos);
				if (frame)
					setInfo(frame, layout.frames.size() > 1
							? QStringLiteral("%1 (%2)").arg(place).arg(placement.frame + 1)
							: place, number);
				break;
			}
			case TecsideRack::Placement::Blank: {
				int units = placement.units;
				while (units > 0) {
					auto it = symbols.blinds.upperBound(units);
					const int piece = it == symbols.blinds.cbegin() ? 1 : (--it).key();
					place_element(symbols.blinds.value(piece), placement.pos
								  + QPointF(0, (placement.units - units) * TecsideRack::unit));
					units -= piece;
				}
				break;
			}
			case TecsideRack::Placement::Shelf:
				place_element(symbols.shelves.value(placement.units), placement.pos);
				break;
			case TecsideRack::Placement::Device: {
				const Source &source = sources.at(placement.device);
				if (Element *front = place_element(source.front, placement.pos))
					setInfo(front, labelOf(source.element), number);
				break;
			}
		}
	}

	if (!layout.notInRack.isEmpty()) {
		QStringList lines{QCoreApplication::translate("TecsideRack", "Not in rack:")};
		for (int index : layout.notInRack) {
			const Element *element = sources.at(index).element;
			const QString label = labelOf(element);
			lines << (label.isEmpty() ? element->name()
									  : QStringLiteral("%1 (%2)").arg(label, element->name()));
		}
		auto *text = new IndependentTextItem(lines.join(QLatin1Char('\n')));
		const QPointF pos = origin + QPointF(layout.frames.size() * TecsideRack::framePitch, 0);
		text->setPos(pos);
		new AddGraphicsObjectCommand(text, diagram, pos, macro);
	}

	stack->push(macro);
	stack->endMacro();
	return diagram;
}

void TecsideRackDrawer::drawWithDialog(QETProject *project, QWidget *parent)
{
	if (!project || project->isReadOnly())
		return;
	const TecsideLocations &locations = project->tecsideLocations();
	const QString title = QCoreApplication::translate("TecsideRack", "Draw rack for location");
	if (!locations.isEnabled() || !locations.count()) {
		QMessageBox::information(parent, title,
			QCoreApplication::translate("TecsideRack", "The project has no locations: add them in Project > Manage locations."));
		return;
	}
	QStringList labels;
	for (const TecsideLocation &location : locations.list())
		labels << locations.label(location);
	bool ok = false;
	const QString chosen = QInputDialog::getItem(parent, title, QCoreApplication::translate("TecsideRack", "Location:"),
												 labels, 0, false, &ok);
	if (!ok)
		return;
	const QString number = locations.list().at(labels.indexOf(chosen)).number;
	QString error;
	if (!draw(project, number, findSymbols(QETApp::companyElementsDir()), &error))
		QMessageBox::warning(parent, title, error);
}
