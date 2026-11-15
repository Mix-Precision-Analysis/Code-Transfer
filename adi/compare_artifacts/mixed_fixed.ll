; ModuleID = '/Users/aleilei/Documents/PerciLLM/20260114/Code-Transfer-Bench/adi/19_fixed.c'
source_filename = "/Users/aleilei/Documents/PerciLLM/20260114/Code-Transfer-Bench/adi/19_fixed.c"
target datalayout = "e-m:o-i64:64-i128:128-n32:64-S128-Fn32"
target triple = "arm64-apple-macosx26.0.0"

@.str = private unnamed_addr constant [7 x i8] c"%.17g\0A\00", align 1

; Function Attrs: nounwind ssp uwtable(sync)
define noundef i32 @main() local_unnamed_addr #0 {
  %1 = tail call dereferenceable_or_null(4000000) ptr @calloc(i64 noundef 1000000, i64 noundef 4) #5
  %2 = tail call dereferenceable_or_null(4000000) ptr @calloc(i64 noundef 1000000, i64 noundef 4) #5
  %3 = tail call dereferenceable_or_null(4000000) ptr @calloc(i64 noundef 1000000, i64 noundef 4) #5
  %4 = tail call dereferenceable_or_null(4000000) ptr @calloc(i64 noundef 1000000, i64 noundef 4) #5
  br label %5

5:                                                ; preds = %45, %0
  %6 = phi i64 [ 0, %0 ], [ %58, %45 ]
  %7 = add nuw nsw i64 %6, 1000
  %8 = mul nuw nsw i64 %6, 4000
  %9 = getelementptr i8, ptr %1, i64 %8
  %10 = insertelement <4 x i64> poison, i64 %7, i64 0
  %11 = shufflevector <4 x i64> %10, <4 x i64> poison, <4 x i32> zeroinitializer
  br label %12

12:                                               ; preds = %12, %5
  %13 = phi i64 [ 0, %5 ], [ %42, %12 ]
  %14 = phi <4 x i64> [ <i64 0, i64 1, i64 2, i64 3>, %5 ], [ %43, %12 ]
  %15 = add <4 x i64> %14, <i64 4, i64 4, i64 4, i64 4>
  %16 = add <4 x i64> %14, <i64 8, i64 8, i64 8, i64 8>
  %17 = add <4 x i64> %14, <i64 12, i64 12, i64 12, i64 12>
  %18 = sub nuw nsw <4 x i64> %11, %14
  %19 = sub nuw nsw <4 x i64> %11, %15
  %20 = sub nuw nsw <4 x i64> %11, %16
  %21 = sub nuw nsw <4 x i64> %11, %17
  %22 = trunc <4 x i64> %18 to <4 x i32>
  %23 = trunc <4 x i64> %19 to <4 x i32>
  %24 = trunc <4 x i64> %20 to <4 x i32>
  %25 = trunc <4 x i64> %21 to <4 x i32>
  %26 = uitofp <4 x i32> %22 to <4 x double>
  %27 = uitofp <4 x i32> %23 to <4 x double>
  %28 = uitofp <4 x i32> %24 to <4 x double>
  %29 = uitofp <4 x i32> %25 to <4 x double>
  %30 = fdiv <4 x double> %26, <double 1.000000e+03, double 1.000000e+03, double 1.000000e+03, double 1.000000e+03>
  %31 = fdiv <4 x double> %27, <double 1.000000e+03, double 1.000000e+03, double 1.000000e+03, double 1.000000e+03>
  %32 = fdiv <4 x double> %28, <double 1.000000e+03, double 1.000000e+03, double 1.000000e+03, double 1.000000e+03>
  %33 = fdiv <4 x double> %29, <double 1.000000e+03, double 1.000000e+03, double 1.000000e+03, double 1.000000e+03>
  %34 = fptrunc <4 x double> %30 to <4 x float>
  %35 = fptrunc <4 x double> %31 to <4 x float>
  %36 = fptrunc <4 x double> %32 to <4 x float>
  %37 = fptrunc <4 x double> %33 to <4 x float>
  %38 = getelementptr float, ptr %9, i64 %13
  %39 = getelementptr i8, ptr %38, i64 16
  %40 = getelementptr i8, ptr %38, i64 32
  %41 = getelementptr i8, ptr %38, i64 48
  store <4 x float> %34, ptr %38, align 4, !tbaa !6
  store <4 x float> %35, ptr %39, align 4, !tbaa !6
  store <4 x float> %36, ptr %40, align 4, !tbaa !6
  store <4 x float> %37, ptr %41, align 4, !tbaa !6
  %42 = add nuw i64 %13, 16
  %43 = add <4 x i64> %14, <i64 16, i64 16, i64 16, i64 16>
  %44 = icmp eq i64 %42, 992
  br i1 %44, label %45, label %12, !llvm.loop !10

45:                                               ; preds = %12
  %46 = trunc <4 x i64> %11 to <4 x i32>
  %47 = add <4 x i32> %46, <i32 -992, i32 -993, i32 -994, i32 -995>
  %48 = uitofp <4 x i32> %47 to <4 x double>
  %49 = fdiv <4 x double> %48, <double 1.000000e+03, double 1.000000e+03, double 1.000000e+03, double 1.000000e+03>
  %50 = fptrunc <4 x double> %49 to <4 x float>
  %51 = getelementptr i8, ptr %9, i64 3968
  store <4 x float> %50, ptr %51, align 4, !tbaa !6
  %52 = trunc <4 x i64> %11 to <4 x i32>
  %53 = add <4 x i32> %52, <i32 -996, i32 -997, i32 -998, i32 -999>
  %54 = uitofp <4 x i32> %53 to <4 x double>
  %55 = fdiv <4 x double> %54, <double 1.000000e+03, double 1.000000e+03, double 1.000000e+03, double 1.000000e+03>
  %56 = fptrunc <4 x double> %55 to <4 x float>
  %57 = getelementptr i8, ptr %9, i64 3984
  store <4 x float> %56, ptr %57, align 4, !tbaa !6
  %58 = add nuw nsw i64 %6, 1
  %59 = icmp eq i64 %58, 1000
  br i1 %59, label %60, label %5, !llvm.loop !14

60:                                               ; preds = %45
  %61 = getelementptr i8, ptr %2, i64 -4000
  %62 = getelementptr inbounds i8, ptr %2, i64 4000
  %63 = getelementptr i8, ptr %1, i64 7996
  br label %64

64:                                               ; preds = %195, %60
  %65 = phi i32 [ 1, %60 ], [ %196, %195 ]
  br label %66

66:                                               ; preds = %127, %64
  %67 = phi i64 [ %130, %127 ], [ 0, %64 ]
  %68 = phi i64 [ %128, %127 ], [ 1, %64 ]
  %69 = mul nuw nsw i64 %67, 4000
  %70 = add i64 %69, 4000
  %71 = getelementptr i8, ptr %4, i64 %70
  %72 = getelementptr i8, ptr %3, i64 %70
  %73 = getelementptr inbounds float, ptr %2, i64 %68
  store float 1.000000e+00, ptr %73, align 4, !tbaa !6
  %74 = mul nuw nsw i64 %68, 1000
  %75 = getelementptr inbounds float, ptr %3, i64 %74
  store float 0.000000e+00, ptr %75, align 4, !tbaa !6
  %76 = getelementptr inbounds float, ptr %4, i64 %74
  store float 1.000000e+00, ptr %76, align 4, !tbaa !6
  %77 = getelementptr float, ptr %1, i64 %68
  %78 = load float, ptr %71, align 4
  %79 = load float, ptr %72, align 4
  br label %80

80:                                               ; preds = %80, %66
  %81 = phi float [ %79, %66 ], [ %87, %80 ]
  %82 = phi float [ %78, %66 ], [ %106, %80 ]
  %83 = phi i64 [ 1, %66 ], [ %108, %80 ]
  %84 = fpext float %81 to double
  %85 = tail call double @llvm.fmuladd.f64(double %84, double -2.000000e+03, double 4.001000e+03)
  %86 = fdiv double 2.000000e+03, %85
  %87 = fptrunc double %86 to float
  %88 = add nuw nsw i64 %83, %74
  %89 = getelementptr inbounds float, ptr %3, i64 %88
  store float %87, ptr %89, align 4, !tbaa !6
  %90 = mul nuw nsw i64 %83, 4000
  %91 = getelementptr i8, ptr %77, i64 %90
  %92 = getelementptr i8, ptr %91, i64 -4
  %93 = load float, ptr %92, align 4, !tbaa !6
  %94 = fpext float %93 to double
  %95 = load float, ptr %91, align 4, !tbaa !6
  %96 = fpext float %95 to double
  %97 = fmul double %96, 0xC09F3C0000000001
  %98 = tail call double @llvm.fmuladd.f64(double %94, double 0x408F400000000001, double %97)
  %99 = getelementptr inbounds i8, ptr %91, i64 4
  %100 = load float, ptr %99, align 4, !tbaa !6
  %101 = fpext float %100 to double
  %102 = tail call double @llvm.fmuladd.f64(double %101, double 0x408F400000000001, double %98)
  %103 = fpext float %82 to double
  %104 = tail call double @llvm.fmuladd.f64(double %103, double 2.000000e+03, double %102)
  %105 = fdiv double %104, %85
  %106 = fptrunc double %105 to float
  %107 = getelementptr inbounds float, ptr %4, i64 %88
  store float %106, ptr %107, align 4, !tbaa !6
  %108 = add nuw nsw i64 %83, 1
  %109 = icmp eq i64 %108, 999
  br i1 %109, label %110, label %80, !llvm.loop !15

110:                                              ; preds = %80
  %111 = getelementptr inbounds i8, ptr %73, i64 3996000
  store float 1.000000e+00, ptr %111, align 4, !tbaa !6
  %112 = getelementptr i8, ptr %73, i64 4000
  br label %113

113:                                              ; preds = %113, %110
  %114 = phi i64 [ 998, %110 ], [ %125, %113 ]
  %115 = add nuw nsw i64 %114, %74
  %116 = getelementptr inbounds float, ptr %3, i64 %115
  %117 = load float, ptr %116, align 4, !tbaa !6
  %118 = mul nuw nsw i64 %114, 4000
  %119 = getelementptr i8, ptr %112, i64 %118
  %120 = load float, ptr %119, align 4, !tbaa !6
  %121 = getelementptr inbounds float, ptr %4, i64 %115
  %122 = load float, ptr %121, align 4, !tbaa !6
  %123 = tail call float @llvm.fmuladd.f32(float %117, float %120, float %122)
  %124 = getelementptr inbounds i8, ptr %73, i64 %118
  store float %123, ptr %124, align 4, !tbaa !6
  %125 = add nsw i64 %114, -1
  %126 = icmp ugt i64 %114, 1
  br i1 %126, label %113, label %127, !llvm.loop !16

127:                                              ; preds = %113
  %128 = add nuw nsw i64 %68, 1
  %129 = icmp eq i64 %128, 999
  %130 = add i64 %67, 1
  br i1 %129, label %131, label %66, !llvm.loop !17

131:                                              ; preds = %127, %191
  %132 = phi i64 [ %194, %191 ], [ 0, %127 ]
  %133 = phi i64 [ %192, %191 ], [ 1, %127 ]
  %134 = mul nuw nsw i64 %132, 4000
  %135 = getelementptr i8, ptr %63, i64 %134
  %136 = add i64 %134, 4000
  %137 = getelementptr i8, ptr %4, i64 %136
  %138 = getelementptr i8, ptr %3, i64 %136
  %139 = mul nuw nsw i64 %133, 1000
  %140 = getelementptr inbounds float, ptr %1, i64 %139
  store float 1.000000e+00, ptr %140, align 4, !tbaa !6
  %141 = getelementptr inbounds float, ptr %3, i64 %139
  store float 0.000000e+00, ptr %141, align 4, !tbaa !6
  %142 = getelementptr inbounds float, ptr %4, i64 %139
  store float 1.000000e+00, ptr %142, align 4, !tbaa !6
  %143 = getelementptr float, ptr %61, i64 %139
  %144 = getelementptr inbounds float, ptr %62, i64 %139
  %145 = load float, ptr %137, align 4
  %146 = load float, ptr %138, align 4
  br label %147

147:                                              ; preds = %147, %131
  %148 = phi float [ %146, %131 ], [ %154, %147 ]
  %149 = phi float [ %145, %131 ], [ %172, %147 ]
  %150 = phi i64 [ 1, %131 ], [ %174, %147 ]
  %151 = fpext float %148 to double
  %152 = tail call double @llvm.fmuladd.f64(double %151, double 0xC08F400000000001, double 0x409F440000000001)
  %153 = fdiv double 0x408F400000000001, %152
  %154 = fptrunc double %153 to float
  %155 = add nuw nsw i64 %150, %139
  %156 = getelementptr inbounds float, ptr %3, i64 %155
  store float %154, ptr %156, align 4, !tbaa !6
  %157 = getelementptr float, ptr %143, i64 %150
  %158 = load float, ptr %157, align 4, !tbaa !6
  %159 = fpext float %158 to double
  %160 = getelementptr inbounds float, ptr %2, i64 %155
  %161 = load float, ptr %160, align 4, !tbaa !6
  %162 = fpext float %161 to double
  %163 = fmul double %162, -3.999000e+03
  %164 = tail call double @llvm.fmuladd.f64(double %159, double 2.000000e+03, double %163)
  %165 = getelementptr inbounds float, ptr %144, i64 %150
  %166 = load float, ptr %165, align 4, !tbaa !6
  %167 = fpext float %166 to double
  %168 = tail call double @llvm.fmuladd.f64(double %167, double 2.000000e+03, double %164)
  %169 = fpext float %149 to double
  %170 = tail call double @llvm.fmuladd.f64(double %169, double 0x408F400000000001, double %168)
  %171 = fdiv double %170, %152
  %172 = fptrunc double %171 to float
  %173 = getelementptr inbounds float, ptr %4, i64 %155
  store float %172, ptr %173, align 4, !tbaa !6
  %174 = add nuw nsw i64 %150, 1
  %175 = icmp eq i64 %174, 999
  br i1 %175, label %176, label %147, !llvm.loop !18

176:                                              ; preds = %147
  %177 = getelementptr inbounds i8, ptr %140, i64 3996
  store float 1.000000e+00, ptr %177, align 4, !tbaa !6
  %178 = load float, ptr %135, align 4
  br label %179

179:                                              ; preds = %179, %176
  %180 = phi float [ %178, %176 ], [ %187, %179 ]
  %181 = phi i64 [ 998, %176 ], [ %189, %179 ]
  %182 = add nuw nsw i64 %181, %139
  %183 = getelementptr inbounds float, ptr %3, i64 %182
  %184 = load float, ptr %183, align 4, !tbaa !6
  %185 = getelementptr inbounds float, ptr %4, i64 %182
  %186 = load float, ptr %185, align 4, !tbaa !6
  %187 = tail call float @llvm.fmuladd.f32(float %184, float %180, float %186)
  %188 = getelementptr inbounds float, ptr %1, i64 %182
  store float %187, ptr %188, align 4, !tbaa !6
  %189 = add nsw i64 %181, -1
  %190 = icmp ugt i64 %181, 1
  br i1 %190, label %179, label %191, !llvm.loop !19

191:                                              ; preds = %179
  %192 = add nuw nsw i64 %133, 1
  %193 = icmp eq i64 %192, 999
  %194 = add i64 %132, 1
  br i1 %193, label %195, label %131, !llvm.loop !20

195:                                              ; preds = %191
  %196 = add nuw nsw i32 %65, 1
  %197 = icmp eq i32 %196, 501
  br i1 %197, label %198, label %64, !llvm.loop !21

198:                                              ; preds = %195
  %199 = load float, ptr %1, align 4, !tbaa !6
  %200 = fpext float %199 to double
  %201 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str, double noundef %200)
  tail call void @free(ptr noundef nonnull %1)
  tail call void @free(ptr noundef %2)
  tail call void @free(ptr noundef nonnull %3)
  tail call void @free(ptr noundef nonnull %4)
  ret i32 0
}

; Function Attrs: mustprogress nofree nounwind willreturn allockind("alloc,zeroed") allocsize(0,1) memory(inaccessiblemem: readwrite)
declare noalias noundef ptr @calloc(i64 noundef, i64 noundef) local_unnamed_addr #1

; Function Attrs: mustprogress nounwind willreturn allockind("free") memory(argmem: readwrite, inaccessiblemem: readwrite)
declare void @free(ptr allocptr nocapture noundef) local_unnamed_addr #2

; Function Attrs: mustprogress nocallback nofree nosync nounwind speculatable willreturn memory(none)
declare double @llvm.fmuladd.f64(double, double, double) #3

; Function Attrs: mustprogress nocallback nofree nosync nounwind speculatable willreturn memory(none)
declare float @llvm.fmuladd.f32(float, float, float) #3

; Function Attrs: nofree nounwind
declare noundef i32 @printf(ptr nocapture noundef readonly, ...) local_unnamed_addr #4

attributes #0 = { nounwind ssp uwtable(sync) "frame-pointer"="non-leaf" "no-trapping-math"="true" "probe-stack"="__chkstk_darwin" "stack-protector-buffer-size"="8" "target-cpu"="apple-m1" "target-features"="+aes,+altnzcv,+bti,+ccdp,+ccidx,+complxnum,+crc,+dit,+dotprod,+flagm,+fp-armv8,+fp16fml,+fptoint,+fullfp16,+jsconv,+lse,+neon,+pauth,+perfmon,+predres,+ras,+rcpc,+rdm,+sb,+sha2,+sha3,+specrestrict,+ssbs,+v8.1a,+v8.2a,+v8.3a,+v8.4a,+v8.5a,+v8a,+zcm,+zcz" }
attributes #1 = { mustprogress nofree nounwind willreturn allockind("alloc,zeroed") allocsize(0,1) memory(inaccessiblemem: readwrite) "alloc-family"="malloc" "frame-pointer"="non-leaf" "no-trapping-math"="true" "probe-stack"="__chkstk_darwin" "stack-protector-buffer-size"="8" "target-cpu"="apple-m1" "target-features"="+aes,+altnzcv,+bti,+ccdp,+ccidx,+complxnum,+crc,+dit,+dotprod,+flagm,+fp-armv8,+fp16fml,+fptoint,+fullfp16,+jsconv,+lse,+neon,+pauth,+perfmon,+predres,+ras,+rcpc,+rdm,+sb,+sha2,+sha3,+specrestrict,+ssbs,+v8.1a,+v8.2a,+v8.3a,+v8.4a,+v8.5a,+v8a,+zcm,+zcz" }
attributes #2 = { mustprogress nounwind willreturn allockind("free") memory(argmem: readwrite, inaccessiblemem: readwrite) "alloc-family"="malloc" "frame-pointer"="non-leaf" "no-trapping-math"="true" "probe-stack"="__chkstk_darwin" "stack-protector-buffer-size"="8" "target-cpu"="apple-m1" "target-features"="+aes,+altnzcv,+bti,+ccdp,+ccidx,+complxnum,+crc,+dit,+dotprod,+flagm,+fp-armv8,+fp16fml,+fptoint,+fullfp16,+jsconv,+lse,+neon,+pauth,+perfmon,+predres,+ras,+rcpc,+rdm,+sb,+sha2,+sha3,+specrestrict,+ssbs,+v8.1a,+v8.2a,+v8.3a,+v8.4a,+v8.5a,+v8a,+zcm,+zcz" }
attributes #3 = { mustprogress nocallback nofree nosync nounwind speculatable willreturn memory(none) }
attributes #4 = { nofree nounwind "frame-pointer"="non-leaf" "no-trapping-math"="true" "probe-stack"="__chkstk_darwin" "stack-protector-buffer-size"="8" "target-cpu"="apple-m1" "target-features"="+aes,+altnzcv,+bti,+ccdp,+ccidx,+complxnum,+crc,+dit,+dotprod,+flagm,+fp-armv8,+fp16fml,+fptoint,+fullfp16,+jsconv,+lse,+neon,+pauth,+perfmon,+predres,+ras,+rcpc,+rdm,+sb,+sha2,+sha3,+specrestrict,+ssbs,+v8.1a,+v8.2a,+v8.3a,+v8.4a,+v8.5a,+v8a,+zcm,+zcz" }
attributes #5 = { allocsize(0,1) }

!llvm.module.flags = !{!0, !1, !2, !3, !4}
!llvm.ident = !{!5}

!0 = !{i32 2, !"SDK Version", [2 x i32] [i32 26, i32 2]}
!1 = !{i32 1, !"wchar_size", i32 4}
!2 = !{i32 8, !"PIC Level", i32 2}
!3 = !{i32 7, !"uwtable", i32 1}
!4 = !{i32 7, !"frame-pointer", i32 1}
!5 = !{!"Apple clang version 17.0.0 (clang-1700.6.3.2)"}
!6 = !{!7, !7, i64 0}
!7 = !{!"float", !8, i64 0}
!8 = !{!"omnipotent char", !9, i64 0}
!9 = !{!"Simple C/C++ TBAA"}
!10 = distinct !{!10, !11, !12, !13}
!11 = !{!"llvm.loop.mustprogress"}
!12 = !{!"llvm.loop.isvectorized", i32 1}
!13 = !{!"llvm.loop.unroll.runtime.disable"}
!14 = distinct !{!14, !11}
!15 = distinct !{!15, !11}
!16 = distinct !{!16, !11}
!17 = distinct !{!17, !11}
!18 = distinct !{!18, !11}
!19 = distinct !{!19, !11}
!20 = distinct !{!20, !11}
!21 = distinct !{!21, !11}
