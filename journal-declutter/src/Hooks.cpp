#include "Hooks.h"

#include "Journal.h"

namespace Hooks
{
	namespace
	{
		using CallbackFn = RE::FxDelegateHandler::CallbackFn;

		CallbackFn* g_origRequestQuestsData{ nullptr };
		CallbackFn* g_origRequestObjectivesData{ nullptr };

		// The game fills TitleList.entryList in here and then calls back into the page
		// (onQuestsDataComplete), which sorts and draws. We filter right after and redraw.
		void RequestQuestsData_Hook(const RE::FxDelegateArgs& a_params)
		{
			if (g_origRequestQuestsData) {
				g_origRequestQuestsData(a_params);
			}
			if (a_params.GetArgCount() < 1) {
				return;
			}
			Journal::Context ctx;
			if (!Journal::GetContext(ctx, a_params.GetMovie(), &a_params[0])) {
				return;
			}
			Journal::OnQuestsPopulated(ctx);
		}

		// The game stores the highlighted quest's objectives on its entry (selectedEntry.objectives);
		// the page copies that array into ObjectiveList afterwards, so filtering here is enough.
		void RequestObjectivesData_Hook(const RE::FxDelegateArgs& a_params)
		{
			if (g_origRequestObjectivesData) {
				g_origRequestObjectivesData(a_params);
			}
			Journal::Context ctx;
			if (!Journal::GetContext(ctx, a_params.GetMovie())) {
				return;
			}
			Journal::OnObjectivesPopulated(ctx);
		}

		class ProcessorWrapper : public RE::FxDelegateHandler::CallbackProcessor
		{
		public:
			explicit ProcessorWrapper(CallbackProcessor* a_inner) :
				_inner(a_inner)
			{}

			void Process(const RE::GString& a_methodName, CallbackFn* a_method) override
			{
				const std::string_view name = a_methodName.c_str() ? a_methodName.c_str() : "";
				if (name == "RequestQuestsData"sv) {
					g_origRequestQuestsData = a_method;
					a_method = &RequestQuestsData_Hook;
					logger::debug("wrapped RequestQuestsData");
				} else if (name == "RequestObjectivesData"sv) {
					g_origRequestObjectivesData = a_method;
					a_method = &RequestObjectivesData_Hook;
					logger::debug("wrapped RequestObjectivesData");
				}
				_inner->Process(a_methodName, a_method);
			}

		private:
			CallbackProcessor* _inner;
		};

		struct Journal_QuestsTab_Accept
		{
			static void thunk(RE::Journal_QuestsTab* a_this, RE::FxDelegateHandler::CallbackProcessor* a_processor)
			{
				ProcessorWrapper wrapper(a_processor);
				func(a_this, &wrapper);
			}
			static inline REL::Relocation<decltype(thunk)> func;

			static void Install()
			{
				REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_Journal_QuestsTab[0] };
				func = vtbl.write_vfunc(0x1, thunk);  // FxDelegateHandler::Accept
			}
		};
	}

	void Install()
	{
		Journal_QuestsTab_Accept::Install();
		logger::info("hooked Journal_QuestsTab::Accept");
	}
}
