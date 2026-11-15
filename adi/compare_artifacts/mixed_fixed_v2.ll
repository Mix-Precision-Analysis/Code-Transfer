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

64:                                               ; preds = %197, %60
  %65 = phi i32 [ 1, %60 ], [ %198, %197 ]
  br label %66

66:                                               ; preds = %128, %64
  %67 = phi i64 [ %131, %128 ], [ 0, %64 ]
  %68 = phi i64 [ %129, %128 ], [ 1, %64 ]
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
  %81 = phi float [ %79, %66 ], [ %85, %80 ]
  %82 = phi float [ %78, %66 ], [ %107, %80 ]
  %83 = phi i64 [ 1, %66 ], [ %109, %80 ]
  %84 = tail call float @llvm.fmuladd.f32(float %81, float 0xC09F3FFFE0000000, float 0x40AF41FFE0000000)
  %85 = fdiv float 0x409F3FFFE0000000, %84
  %86 = add nuw nsw i64 %83, %74
  %87 = getelementptr inbounds float, ptr %3, i64 %86
  store float %85, ptr %87, align 4, !tbaa !6
  %88 = mul nuw nsw i64 %83, 4000
  %89 = getelementptr i8, ptr %77, i64 %88
  %90 = getelementptr i8, ptr %89, i64 -4
  %91 = load float, ptr %90, align 4, !tbaa !6
  %92 = fmul float %91, 0x408F3FFFE0000000
  %93 = fpext float %92 to double
  %94 = load float, ptr %89, align 4, !tbaa !6
  %95 = fpext float %94 to double
  %96 = tail call double @llvm.fmuladd.f64(double %95, double 0xC09F3BFFE0000000, double %93)
  %97 = getelementptr inbounds i8, ptr %89, i64 4
  %98 = load float, ptr %97, align 4, !tbaa !6
  %99 = fmul float %98, 0xC08F3FFFE0000000
  %100 = fpext float %99 to double
  %101 = fsub double %96, %100
  %102 = fmul float %82, 0xC09F3FFFE0000000
  %103 = fpext float %102 to double
  %104 = fsub double %101, %103
  %105 = fpext float %84 to double
  %106 = fdiv double %104, %105
  %107 = fptrunc double %106 to float
  %108 = getelementptr inbounds float, ptr %4, i64 %86
  store float %107, ptr %108, align 4, !tbaa !6
  %109 = add nuw nsw i64 %83, 1
  %110 = icmp eq i64 %109, 999
  br i1 %110, label %111, label %80, !llvm.loop !15

111:                                              ; preds = %80
  %112 = getelementptr inbounds i8, ptr %73, i64 3996000
  store float 1.000000e+00, ptr %112, align 4, !tbaa !6
  %113 = getelementptr i8, ptr %73, i64 4000
  br label %114

114:                                              ; preds = %114, %111
  %115 = phi i64 [ 998, %111 ], [ %126, %114 ]
  %116 = add nuw nsw i64 %115, %74
  %117 = getelementptr inbounds float, ptr %3, i64 %116
  %118 = load float, ptr %117, align 4, !tbaa !6
  %119 = mul nuw nsw i64 %115, 4000
  %120 = getelementptr i8, ptr %113, i64 %119
  %121 = load float, ptr %120, align 4, !tbaa !6
  %122 = getelementptr inbounds float, ptr %4, i64 %116
  %123 = load float, ptr %122, align 4, !tbaa !6
  %124 = tail call float @llvm.fmuladd.f32(float %118, float %121, float %123)
  %125 = getelementptr inbounds i8, ptr %73, i64 %119
  store float %124, ptr %125, align 4, !tbaa !6
  %126 = add nsw i64 %115, -1
  %127 = icmp ugt i64 %115, 1
  br i1 %127, label %114, label %128, !llvm.loop !16

128:                                              ; preds = %114
  %129 = add nuw nsw i64 %68, 1
  %130 = icmp eq i64 %129, 999
  %131 = add i64 %67, 1
  br i1 %130, label %132, label %66, !llvm.loop !17

132:                                              ; preds = %128, %193
  %133 = phi i64 [ %196, %193 ], [ 0, %128 ]
  %134 = phi i64 [ %194, %193 ], [ 1, %128 ]
  %135 = mul nuw nsw i64 %133, 4000
  %136 = getelementptr i8, ptr %63, i64 %135
  %137 = add i64 %135, 4000
  %138 = getelementptr i8, ptr %4, i64 %137
  %139 = getelementptr i8, ptr %3, i64 %137
  %140 = mul nuw nsw i64 %134, 1000
  %141 = getelementptr inbounds float, ptr %1, i64 %140
  store float 1.000000e+00, ptr %141, align 4, !tbaa !6
  %142 = getelementptr inbounds float, ptr %3, i64 %140
  store float 0.000000e+00, ptr %142, align 4, !tbaa !6
  %143 = getelementptr inbounds float, ptr %4, i64 %140
  store float 1.000000e+00, ptr %143, align 4, !tbaa !6
  %144 = getelementptr float, ptr %61, i64 %140
  %145 = getelementptr inbounds float, ptr %62, i64 %140
  %146 = load float, ptr %138, align 4
  %147 = load float, ptr %139, align 4
  br label %148

148:                                              ; preds = %148, %132
  %149 = phi float [ %147, %132 ], [ %153, %148 ]
  %150 = phi float [ %146, %132 ], [ %174, %148 ]
  %151 = phi i64 [ 1, %132 ], [ %176, %148 ]
  %152 = tail call float @llvm.fmuladd.f32(float %149, float 0xC08F3FFFE0000000, float 0x409F43FFE0000000)
  %153 = fdiv float 0x408F3FFFE0000000, %152
  %154 = add nuw nsw i64 %151, %140
  %155 = getelementptr inbounds float, ptr %3, i64 %154
  store float %153, ptr %155, align 4, !tbaa !6
  %156 = getelementptr float, ptr %144, i64 %151
  %157 = load float, ptr %156, align 4, !tbaa !6
  %158 = fmul float %157, 0x409F3FFFE0000000
  %159 = fpext float %158 to double
  %160 = getelementptr inbounds float, ptr %2, i64 %154
  %161 = load float, ptr %160, align 4, !tbaa !6
  %162 = fpext float %161 to double
  %163 = tail call double @llvm.fmuladd.f64(double %162, double 0xC0AF3DFFE0000000, double %159)
  %164 = getelementptr inbounds float, ptr %145, i64 %151
  %165 = load float, ptr %164, align 4, !tbaa !6
  %166 = fmul float %165, 0xC09F3FFFE0000000
  %167 = fpext float %166 to double
  %168 = fsub double %163, %167
  %169 = fmul float %150, 0xC08F3FFFE0000000
  %170 = fpext float %169 to double
  %171 = fsub double %168, %170
  %172 = fpext float %152 to double
  %173 = fdiv double %171, %172
  %174 = fptrunc double %173 to float
  %175 = getelementptr inbounds float, ptr %4, i64 %154
  store float %174, ptr %175, align 4, !tbaa !6
  %176 = add nuw nsw i64 %151, 1
  %177 = icmp eq i64 %176, 999
  br i1 %177, label %178, label %148, !llvm.loop !18

178:                                              ; preds = %148
  %179 = getelementptr inbounds i8, ptr %141, i64 3996
  store float 1.000000e+00, ptr %179, align 4, !tbaa !6
  %180 = load float, ptr %136, align 4
  br label %181

181:                                              ; preds = %181, %178
  %182 = phi float [ %180, %178 ], [ %189, %181 ]
  %183 = phi i64 [ 998, %178 ], [ %191, %181 ]
  %184 = add nuw nsw i64 %183, %140
  %185 = getelementptr inbounds float, ptr %3, i64 %184
  %186 = load float, ptr %185, align 4, !tbaa !6
  %187 = getelementptr inbounds float, ptr %4, i64 %184
  %188 = load float, ptr %187, align 4, !tbaa !6
  %189 = tail call float @llvm.fmuladd.f32(float %186, float %182, float %188)
  %190 = getelementptr inbounds float, ptr %1, i64 %184
  store float %189, ptr %190, align 4, !tbaa !6
  %191 = add nsw i64 %183, -1
  %192 = icmp ugt i64 %183, 1
  br i1 %192, label %181, label %193, !llvm.loop !19

193:                                              ; preds = %181
  %194 = add nuw nsw i64 %134, 1
  %195 = icmp eq i64 %194, 999
  %196 = add i64 %133, 1
  br i1 %195, label %197, label %132, !llvm.loop !20

197:                                              ; preds = %193
  %198 = add nuw nsw i32 %65, 1
  %199 = icmp eq i32 %198, 501
  br i1 %199, label %200, label %64, !llvm.loop !21

200:                                              ; preds = %197
  %201 = load float, ptr %1, align 4, !tbaa !6
  %202 = fpext float %201 to double
  %203 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str, double noundef %202)
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
declare float @llvm.fmuladd.f32(float, float, float) #3

; Function Attrs: mustprogress nocallback nofree nosync nounwind speculatable willreturn memory(none)
declare double @llvm.fmuladd.f64(double, double, double) #3

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
