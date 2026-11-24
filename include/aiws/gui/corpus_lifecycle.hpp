#pragma once

namespace aiws::gui {

class CorpusLifecycle {
private:
    struct State {
        virtual ~State() = default;

        virtual void document_added(CorpusLifecycle& lifecycle) const;
        virtual void reset(CorpusLifecycle& lifecycle) const;
        virtual void request_build(CorpusLifecycle&) const {}
        virtual void build_succeeded(CorpusLifecycle&) const {}
        virtual void build_failed(CorpusLifecycle&) const {}
        virtual void exit(CorpusLifecycle& lifecycle) const;

        virtual bool can_build() const { return false; }
        virtual bool can_search() const { return false; }
        virtual bool is_building() const { return false; }

        virtual const char* added_message() const {
            return "Document added.";
        }

        virtual const char* reset_message() const {
            return "Application reset.";
        }

        virtual const char* corpus_after_add() const {
            return "Corpus: Not built";
        }
    };

    struct Empty final : State {};

    struct NeedsBuild final : State {
        bool can_build() const override { return true; }
        void request_build(CorpusLifecycle& lifecycle) const override;
    };

    struct Building final : State {
        bool is_building() const override { return true; }

        const char* added_message() const override {
            return "Workspace changed during build. Rebuild required.";
        }

        const char* reset_message() const override {
            return "Workspace changed during build. Rebuild required.";
        }

        void build_succeeded(CorpusLifecycle& lifecycle) const override;
        void build_failed(CorpusLifecycle& lifecycle) const override;
    };

    struct Current final : State {
        bool can_build() const override { return true; }
        bool can_search() const override { return true; }

        const char* corpus_after_add() const override {
            return "Corpus: Stale (rebuild required)";
        }

        void request_build(CorpusLifecycle& lifecycle) const override;
    };

    struct Closing final : State {
        void document_added(CorpusLifecycle&) const override {}
        void reset(CorpusLifecycle&) const override {}
    };

    Empty empty_;
    NeedsBuild needs_build_;
    Building building_;
    Current current_;
    Closing closing_;

    // All five objects belong to this GUI window.
    const State* state_ = &empty_;

public:
    CorpusLifecycle() = default;
    CorpusLifecycle(const CorpusLifecycle&) = delete;
    CorpusLifecycle& operator=(const CorpusLifecycle&) = delete;

    bool can_build() const { return state_->can_build(); }
    bool can_search() const { return state_->can_search(); }
    bool is_building() const { return state_->is_building(); }

    const char* added_message() const { return state_->added_message(); }
    const char* reset_message() const { return state_->reset_message(); }
    const char* corpus_after_add() const { return state_->corpus_after_add(); }

    void document_added() { state_->document_added(*this); }
    void reset() { state_->reset(*this); }
    void request_build() { state_->request_build(*this); }
    void build_succeeded() { state_->build_succeeded(*this); }
    void build_failed() { state_->build_failed(*this); }
    void exit() { state_->exit(*this); }
};

inline void CorpusLifecycle::State::document_added(
    CorpusLifecycle& lifecycle) const {
    lifecycle.state_ = &lifecycle.needs_build_;
}

inline void CorpusLifecycle::State::reset(
    CorpusLifecycle& lifecycle) const {
    lifecycle.state_ = &lifecycle.empty_;
}

inline void CorpusLifecycle::State::exit(
    CorpusLifecycle& lifecycle) const {
    lifecycle.state_ = &lifecycle.closing_;
}

inline void CorpusLifecycle::NeedsBuild::request_build(
    CorpusLifecycle& lifecycle) const {
    lifecycle.state_ = &lifecycle.building_;
}

inline void CorpusLifecycle::Current::request_build(
    CorpusLifecycle& lifecycle) const {
    lifecycle.state_ = &lifecycle.building_;
}

inline void CorpusLifecycle::Building::build_succeeded(
    CorpusLifecycle& lifecycle) const {
    lifecycle.state_ = &lifecycle.current_;
}

inline void CorpusLifecycle::Building::build_failed(
    CorpusLifecycle& lifecycle) const {
    lifecycle.state_ = &lifecycle.needs_build_;
}

}  // namespace aiws::gui