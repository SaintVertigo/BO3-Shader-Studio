GLSL converter regression corpus.

Run Tools -> Run GLSL Converter Regression Suite.
Each small file isolates a conversion pattern that has caused or could cause BO3/FXC problems.
When a real shader exposes a new converter bug, reduce it to a small case and add it here so the bug stays fixed permanently.

0.18.30 adds GLSL relational builtin coverage: less/greater/equal/not families.
0.18.37-0.18.39 add real Shaders21k constructor, macro, matrix, reserved-name and fallback-return coverage.
0.18.40 expands to 53 cases with early struct factories, struct arrays, matrix members/scalar chains, comma-list reserved names, uninitialized locals, final default switch arms, and inout structs.

59_multiline_empty_macro_argument.glsl - multiline one-parameter macro called with an empty token sequence

60_long_loop_no_texture_dynamic_index.glsl - long non-texture loop with dynamic vector-component l-value must not receive [loop]

0.18.49 adds exact-source regression coverage for the seven remaining failures from the 250-shader / seed-1337 mixed Shaders21k corpus sample:
61-62/64/67 TwiGL geek-mode r/t/o compatibility, 63 macro metaprogramming, 65 dynamic out-of-range vector write compatibility, and 66 relaxed vector-to-int scalar casting.

0.18.50 fixes the final two failures still exposed by exact-source cases 63 and 64:
68_stpq_vector_swizzle.glsl - GLSL stpq vector swizzles lower to xyzw while a real struct field named s is preserved
69_comma_uninitialized_locals.glsl - uninitialized locals in a comma declaration list are deterministically zero-initialized for FXC

0.18.58 adds simultaneous converter-side macro parameter substitution:
75_simultaneous_macro_parameter_substitution.glsl - nested macro expansion must not let a later formal parameter rewrite identifier tokens inside an argument already substituted for an earlier formal parameter


0.18.59 hardens custom-struct overload lowering for legacy FXC:
76_swapped_forwarder_member_names.glsl - swapped-forwarder substitution must replace formal variables without rewriting `.a` / `.b` member names
77_exact_struct_scalar_overload.glsl - exact custom-struct/scalar overload calls nested in aggregate forwarding functions are statically resolved before FXC

0.18.60 extends exact overload inference through nested unary calls:
78_nested_unary_return_overload.glsl - infer an exact unary custom-struct return type (for example `ab(p)`) so an outer custom-struct/scalar overload can be statically resolved before FXC

0.18.61 makes exact custom-struct overload lowering iterative:
79_iterative_exact_struct_overload.glsl - when inlining an exact aggregate overload exposes another exact custom-struct overload call, revisit the new expression on a bounded follow-up pass before FXC

0.18.62 makes scoped local-shadow renaming member/swizzle-safe:
80_local_shadow_preserves_member_swizzle.glsl - renaming a local such as `float z = z(...)` must rename bare local uses while preserving vector/struct members such as `p.z` and `p . z`

0.18.63 makes matrix identifier typing scope-aware:
81_reused_vector_matrix_identifier.glsl - a vec3 parameter must remain vector-valued for component-wise multiplication even when the same identifier is used as a mat3 parameter elsewhere

0.18.64 narrows local/function shadow renaming to declarations whose initializer actually calls the same-named function:
82_local_function_name_macro_reference.glsl - a local that merely shares a name with a function must stay unchanged so macros that reference that local remain valid


0.18.65 extends exact overload inference through two-argument constructor-like helper functions:
83_nested_binary_constructor_helper_overload.glsl - infer an exact custom-struct return from helpers such as c11(.5,.0), then carry that type through a nested unary call so the surrounding struct overload can be resolved before FXC


0.18.66 extends exact overload inference through scalar/vector arithmetic and strengthens regression expectations:
83_nested_binary_constructor_helper_overload.glsl - now requires the converter to report that exact custom-struct overload inlining actually ran
84_scalar_arithmetic_exact_overload_inference.glsl - infer scalar arithmetic such as a.a/a.b+.5 through a unary constructor helper so the surrounding exact struct overload is normalized before FXC

0.18.67 makes exact custom-struct overload lowering inside-out:
- Collects nested exact overload calls before editing a return expression.
- Lowers the deepest non-overlapping calls first and defers overlapping parents to the next bounded pass.
- Prevents an outer custom-struct expansion from burying an unresolved inner overload beneath generated member accesses.
- Adds case 85 for nested `mu(su(...), ...)` lowering order.


0.18.68 makes out/inout initialization preprocessor-safe:
86_inout_macro_generic_type.glsl - generic macro type parameters inside #define templates must not be mistaken for concrete HLSL types by the definite-assignment initializer

0.18.69 makes global const promotion preprocessor-safe:
- Case 87 ensures braces inside a #define cannot poison the top-level scope tracker and prevent a following GLSL const from becoming FXC-safe static const.

Case 88 covers exact custom-struct overload calls in assignments inside multi-statement function bodies. The converter must normalize the call itself rather than relying on FXC to choose among structurally similar user types.

Case 89 covers type inference through object-like GLSL value macros whose expansion uses a type-preserving intrinsic. This keeps nested scalar expressions from hiding an otherwise exact struct/scalar overload.

Case 90 covers direct exact unary overload calls across structurally similar custom types.

Case 91 covers a struct-valued function-like macro invocation that hides an exact unary custom-struct overload from the normal call scanner.

Case 92 covers an eight-level nested exact-overload expression so inside-out normalization cannot silently stop at the former six-pass safety cap.

Case 93 covers type inference through parenthesized member primaries such as `(p).b`, including parentheses introduced by earlier safe overload substitutions.

Case 94 covers transitive function-like macro wrappers where a struct-valued outer macro reaches a custom-struct overload only through another safe macro.

Case 95 covers exact custom-struct overload resolution through helpers with three or more arguments, including carrying their proven return type into a surrounding overload call.

Case 96 covers finite exact-overload nesting deeper than sixteen levels so the convergence safety limit cannot leave a known deferred parent for FXC.

Case 97 covers scoped identifier reuse between vectors and custom structs whose fields use GLSL swizzle-alphabet letters. The vector `.st` must become `.xy`, while custom-struct `.p` and `.t` fields remain literal members.

Case 98 covers macro-safe matrix multiplication when a generic function-like macro parameter is multiplied by an explicit matrix constructor.

Case 99 covers a matrix-matrix product whose generated `mul()` result participates in a following matrix-vector multiplication.

Case 100 covers macro call-site vector proof through comma-separated declarations, where the relevant argument identifiers are not the declaration list's first name.

Case 101 covers side-effectful arguments to nested exact custom-struct overloads. Normalization must preserve real function-call boundaries transitively so a potentially state-changing user function is evaluated exactly once even when both the immediate and outer callees reference their parameters more than once.

Case 102 covers a matrix macro used directly with a vector and transitively through another macro with a scalar. A direct call cannot authorize a global body rewrite that changes the hidden scalar/matrix invocation.

0.19.0 completes the macro-metaprogramming and public-corpus compatibility pass, adds the headless regression CLI and incremental `build_regression.bat` workflow, and expands this suite to 102 focused cases. The release was validated with all 102 regressions plus a cache-disabled random 250-shader public-corpus sample using seed 1337, with no failures or skips.
103_reserved_sampler_identifier.glsl - GLSL function parameters named `sampler` are renamed before sampler2D lowering so FXC does not parse the identifier as the reserved HLSL `sampler` keyword; textureSize/texture calls keep the renamed resource argument.

Case 104 covers object-like GLSL aliases to Shadertoy built-ins when the alias is reused as a function parameter. `#define t iTime` plus `float f(..., float t)` must be expanded and then scope-renamed before BO3's generated `iTime` macro reaches FXC; otherwise the signature becomes `float (GetTime())` and fails with X3000.
