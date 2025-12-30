/*
 * This source file is part of RmlUi, the HTML/CSS Interface Middleware
 *
 * For the latest information, see http://github.com/mikke89/RmlUi
 *
 * Copyright (c) 2008-2010 CodePoint Ltd, Shift Technology Ltd
 * Copyright (c) 2019-2023 The RmlUi Team, and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include "ElementEffects.h"
#include "../../Include/RmlUi/Core/ComputedValues.h"
#include "../../Include/RmlUi/Core/Decorator.h"
#include "../../Include/RmlUi/Core/Element.h"
#include "../../Include/RmlUi/Core/ElementDocument.h"
#include "../../Include/RmlUi/Core/ElementUtilities.h"
#include "../../Include/RmlUi/Core/Filter.h"
#include "../../Include/RmlUi/Core/Profiling.h"
#include "../../Include/RmlUi/Core/StyleSheet.h"

namespace Rml {

ElementEffects::ElementEffects(Element* _element) : element(_element) {}

ElementEffects::~ElementEffects()
{
	ReleaseEffects();
}

void ElementEffects::InstanceEffects()
{
	if (!effects_dirty)
		return;

	effects_dirty = false;
	effects_data_dirty = true;

	RMLUI_ZoneScopedC(0xB22222);
	ReleaseEffects();

	RenderManager* render_manager = element->GetRenderManager();
	if (!render_manager)
	{
		RMLUI_ERRORMSG("Decorators are being instanced before a render manager is available. Is this element attached to the document?");
		return;
	}

	const ComputedValues& computed = element->GetComputedValues();

	if (computed.has_decorator() || computed.has_mask_image())
	{
		const StyleSheet* style_sheet = element->GetStyleSheet();
		if (!style_sheet)
			return;

		for (const auto id : {PropertyId::Decorator, PropertyId::MaskImage})
		{
			const Property* property = element->GetLocalProperty(id);
			if (!property || property->unit != Unit::DECORATOR)
				continue;

			DecoratorsPtr decorators_ptr = property->Get<DecoratorsPtr>();
			if (!decorators_ptr)
				continue;

			PropertySource document_source("", 0, "");
			const PropertySource* source = property->source.get();

			if (!source)
			{
				if (ElementDocument* document = element->GetOwnerDocument())
				{
					document_source.path = document->GetSourceURL();
					source = &document_source;
				}
			}

			const DecoratorPtrList& decorator_list = style_sheet->InstanceDecorators(*render_manager, *decorators_ptr, source);
			RMLUI_ASSERT(decorator_list.empty() || decorator_list.size() == decorators_ptr->list.size());

			DecoratorEntryList& decorators_target = (id == PropertyId::Decorator ? decorators : mask_images);
			decorators_target.reserve(decorators_ptr->list.size());

			for (size_t i = 0; i < decorator_list.size() && i < decorators_ptr->list.size(); i++)
			{
				const SharedPtr<const Decorator>& decorator = decorator_list[i];
				if (decorator)
				{
					DecoratorEntry entry;
					entry.decorator_data = 0;
					entry.decorator = decorator;
					entry.paint_area = decorators_ptr->list[i].paint_area;
					if (entry.paint_area == BoxArea::Auto)
						entry.paint_area = (id == PropertyId::Decorator ? BoxArea::Padding : BoxArea::Border);

					RMLUI_ASSERT(entry.paint_area >= BoxArea::Border && entry.paint_area <= BoxArea::Content);
					decorators_target.push_back(std::move(entry));
				}
			}
		}
	}

	if (computed.has_filter() || computed.has_backdrop_filter())
	{
		for (const auto id : {PropertyId::Filter, PropertyId::BackdropFilter})
		{
			const Property* property = element->GetLocalProperty(id);
			if (!property || property->unit != Unit::FILTER)
				continue;

			FiltersPtr filters_ptr = property->Get<FiltersPtr>();
			if (!filters_ptr)
				continue;

			FilterEntryList& list = (id == PropertyId::Filter ? filters : backdrop_filters);
			list.reserve(filters_ptr->list.size());

			for (const FilterDeclaration& declaration : filters_ptr->list)
			{
				SharedPtr<const Filter> filter = declaration.instancer->InstanceFilter(declaration.type, declaration.properties);
				if (filter)
				{
					list.push_back({std::move(filter), CompiledFilter{}});
				}
				else
				{
					const auto& source = property->source;
					Log::Message(Log::LT_WARNING, "Filter '%s' in '%s' could not be instanced, declared at %s:%d", declaration.type.c_str(),
						filters_ptr->value.c_str(), source ? source->path.c_str() : "", source ? source->line_number : -1);
				}
			}
		}
	}
}

void ElementEffects::ReloadEffectsData()
{
	if (effects_data_dirty)
	{
		effects_data_dirty = false;

		bool decorator_data_failed = false;
		for (DecoratorEntryList* list : {&decorators, &mask_images})
		{
			for (DecoratorEntry& decorator : *list)
			{
				const DecoratorDataHandle old_data = decorator.decorator_data;

				decorator.decorator_data = decorator.decorator->GenerateElementData(element, decorator.paint_area);
				if (!decorator.decorator_data)
					decorator_data_failed = true;

				// Release old element data after generating new data, so that the decorator can reuse any cache.
				if (old_data)
					decorator.decorator->ReleaseElementData(old_data);
			}
		}

		if (decorator_data_failed)
			Log::Message(Log::LT_WARNING, "Could not generate decorator element data: %s", element->GetAddress().c_str());

		bool filter_compile_failed = false;
		for (FilterEntryList* list : {&filters, &backdrop_filters})
		{
			for (FilterEntry& filter : *list)
			{
				filter.compiled = filter.filter->CompileFilter(element);
				if (!filter.compiled)
					filter_compile_failed = true;
			}
		}

		if (filter_compile_failed)
			Log::Message(Log::LT_WARNING, "Could not compile filter on element: %s", element->GetAddress().c_str());
	}
}

void ElementEffects::ReleaseEffects()
{
	for (DecoratorEntryList* list : {&decorators, &mask_images})
	{
		for (DecoratorEntry& decorator : *list)
		{
			if (decorator.decorator_data)
				decorator.decorator->ReleaseElementData(decorator.decorator_data);
		}
		list->clear();
	}

	filters.clear();
	backdrop_filters.clear();
}

void ElementEffects::RenderEffects(RenderStage render_stage)
{
	InstanceEffects();
	ReloadEffectsData();

	if (!decorators.empty())
	{
		if (render_stage == RenderStage::Decoration)
		{
			// Render the decorators attached to this element in its current state.
			// Render from back to front for correct render order.
			for (int i = (int)decorators.size() - 1; i >= 0; i--)
			{
				DecoratorEntry& decorator = decorators[i];
				if (decorator.decorator_data)
					decorator.decorator->RenderElement(element, decorator.decorator_data);
			}
		}
	}

	if (filters.empty() && backdrop_filters.empty() && mask_images.empty())
		return;

	RenderManager* render_manager = element->GetRenderManager();
	if (!render_manager)
		return;

	Rectanglei initial_scissor_region = render_manager->GetScissorRegion();

	auto ApplyClippingRegion = [this, &render_manager](PropertyId filter_id) {
		RMLUI_ASSERT(filter_id == PropertyId::Filter || filter_id == PropertyId::BackdropFilter);

		const bool force_clip_to_self_border_box = (filter_id == PropertyId::BackdropFilter);
		ElementUtilities::SetClippingRegion(element, force_clip_to_self_border_box);

		// Find the region being affected by the active filters and apply it as a scissor.
		Rectanglef filter_region = Rectanglef::MakeInvalid();
		ElementUtilities::GetBoundingBox(filter_region, element, force_clip_to_self_border_box ? BoxArea::Border : BoxArea::Auto);

		// The filter property may draw outside our normal clipping region due to ink overflow.
		if (filter_id == PropertyId::Filter)
		{
			for (const auto& filter : filters)
				filter.filter->ExtendInkOverflow(element, filter_region);
		}

		Math::ExpandToPixelGrid(filter_region);

		Rectanglei scissor_region = Rectanglei(filter_region).IntersectIfValid(render_manager->GetScissorRegion());
		render_manager->SetScissorRegion(scissor_region);
	};
	auto GetBackdropScissorRegion = [this, &initial_scissor_region](bool extend_for_filters) {
		Rectanglef filter_region = Rectanglef::MakeInvalid();
		ElementUtilities::GetBoundingBox(filter_region, element, BoxArea::Border);
		if (extend_for_filters)
		{
			for (const auto& filter : backdrop_filters)
				filter.filter->ExtendInkOverflow(element, filter_region);
		}
		Math::ExpandToPixelGrid(filter_region);
		return Rectanglei(filter_region).IntersectIfValid(initial_scissor_region);
	};

	if (render_stage == RenderStage::Enter)
	{
		const LayerHandle backdrop_source_layer = render_manager->GetTopLayer();

		if (!filters.empty() || !mask_images.empty())
		{
			render_manager->PushLayer();
		}

		if (!backdrop_filters.empty())
		{
			const LayerHandle backdrop_destination_layer = render_manager->GetTopLayer();

			FilterHandleList filter_handles;
			for (auto& filter : backdrop_filters)
				filter.compiled.AddHandleTo(filter_handles);
			const Rectanglei base_scissor_region = GetBackdropScissorRegion(false);
			const Rectanglei extended_scissor_region = GetBackdropScissorRegion(true);
			const bool needs_temp_layer = (extended_scissor_region != base_scissor_region);

			if (needs_temp_layer)
			{
				// We only need a temporary buffer when reading from outside the element bounds (eg. blur, drop-shadow).
				render_manager->SetScissorRegion(extended_scissor_region);
				render_manager->PushLayer();
				const LayerHandle backdrop_temp_layer = render_manager->GetTopLayer();

				// Render the backdrop filters in the extended scissor region including any ink overflow.
				render_manager->CompositeLayers(backdrop_source_layer, backdrop_temp_layer, BlendMode::Blend, filter_handles);

				// Then composite the filter output to our destination while applying our clipping region, including any border-radius.
				ApplyClippingRegion(PropertyId::BackdropFilter);
				render_manager->CompositeLayers(backdrop_temp_layer, backdrop_destination_layer, BlendMode::Blend, {});
				render_manager->PopLayer();
				render_manager->SetScissorRegion(initial_scissor_region);
			}
			else
			{
				ApplyClippingRegion(PropertyId::BackdropFilter);
				render_manager->CompositeLayers(backdrop_source_layer, backdrop_destination_layer, BlendMode::Blend, filter_handles);
				render_manager->SetScissorRegion(initial_scissor_region);
			}
		}
	}
	else if (render_stage == RenderStage::Exit)
	{
		if (!filters.empty() || !mask_images.empty())
		{
			ApplyClippingRegion(PropertyId::Filter);

			CompiledFilter mask_image_filter;
			FilterHandleList filter_handles;
			filter_handles.reserve(filters.size() + (mask_images.empty() ? 0 : 1));

			for (auto& filter : filters)
				filter.compiled.AddHandleTo(filter_handles);

			if (!mask_images.empty())
			{
				render_manager->PushLayer();

				for (int i = (int)mask_images.size() - 1; i >= 0; i--)
				{
					DecoratorEntry& mask_image = mask_images[i];
					if (mask_image.decorator_data)
						mask_image.decorator->RenderElement(element, mask_image.decorator_data);
				}
				mask_image_filter = render_manager->SaveLayerAsMaskImage();
				mask_image_filter.AddHandleTo(filter_handles);
				render_manager->PopLayer();
			}

			render_manager->CompositeLayers(render_manager->GetTopLayer(), render_manager->GetNextLayer(), BlendMode::Blend, filter_handles);
			render_manager->PopLayer();
			render_manager->SetScissorRegion(initial_scissor_region);
		}
	}
}

void ElementEffects::ExtendInkOverflowBounds(Rectanglef& bounds)
{
	InstanceEffects();

	if (filters.empty() && backdrop_filters.empty())
		return;

	for (const auto& filter : filters)
		filter.filter->ExtendInkOverflow(element, bounds);
	for (const auto& filter : backdrop_filters)
		filter.filter->ExtendInkOverflow(element, bounds);
}

void ElementEffects::DirtyEffects()
{
	effects_dirty = true;
}

void ElementEffects::DirtyEffectsData()
{
	effects_data_dirty = true;
}

} // namespace Rml
