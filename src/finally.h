template <typename F>
struct FinalAction 
{
	FinalAction(F f) : clean{f} {}
	~FinalAction() { if (enabled) clean(); }
    void disable() { enabled = false; };

	private:
    F clean;
    bool enabled{true}; 
};

template <typename F>
FinalAction<F> finally(F f) 
{
	return FinalAction<F>(f); 
};