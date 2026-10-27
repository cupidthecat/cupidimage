#include "pdf_render_internal.h"
#include <math.h>
#include <string.h>

typedef struct {
    double domain[2], c0[4], c1[4], range[8], exponent;
    int components, has_range;
} pdf_exponential_function;

static int pdf_shading_array(const pdf_render_state *st, const pdf_object_view *parent,
                             const char *key, double *values, int count) {
    size_t pos;
    if (!cupidimage_pdf_page_value(parent, key, &pos)) return 0;
    pdf_object_view owned = {0};
    const pdf_object_view *view = parent;
    int object, ok = 0;
    for (unsigned depth = 0; cupidimage_pdf_page_reference(view, pos, &object); depth++) {
        if (depth == 16) goto done;
        pdf_object_view next = {0};
        if (!cupidimage_pdf_get_object_content(st->document, st->document_size, object, &next)) goto done;
        cupidimage_pdf_object_view_release(&owned); owned = next; view = &owned; pos = 0;
        cupidimage_pdf_skip_ws_and_comments(view->data, view->len, &pos);
    }
    if (pos == view->len || view->data[pos++] != '[') goto done;
    for (int i = 0; i < count; i++) {
        cupidimage_pdf_skip_ws_and_comments(view->data, view->len, &pos);
        if (!cupidimage_pdf_read_number(st, view, pos, 0, &values[i])) goto done;
        double ignored;
        if (cupidimage_pdf_page_reference(view, pos, &object)) {
            cupidimage_pdf_parse_pdf_number(view->data, view->len, &pos, &ignored);
            cupidimage_pdf_skip_ws_and_comments(view->data, view->len, &pos);
            cupidimage_pdf_parse_pdf_number(view->data, view->len, &pos, &ignored);
            cupidimage_pdf_skip_ws_and_comments(view->data, view->len, &pos); pos++;
        } else if (!cupidimage_pdf_parse_pdf_number(view->data, view->len, &pos, &ignored)) goto done;
    }
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, &pos);
    ok = pos < view->len && view->data[pos] == ']';
done:
    cupidimage_pdf_object_view_release(&owned);
    return ok;
}

static int pdf_shading_function(const pdf_render_state *st, const pdf_object_view *shading,
                                int components, pdf_exponential_function *function) {
    pdf_object_view view = {0};
    int type, ok = 0;
    size_t pos;
    if (!cupidimage_pdf_image_dictionary(st, shading, "Function", &view) ||
        !cupidimage_pdf_image_integer(st, &view, "FunctionType", &type) || type != 2 ||
        !pdf_shading_array(st, &view, "Domain", function->domain, 2) || function->domain[0] >= function->domain[1] ||
        !cupidimage_pdf_page_value(&view, "N", &pos) ||
        !cupidimage_pdf_read_number(st, &view, pos, 0, &function->exponent)) goto done;
    function->components = components;
    if ((floor(function->exponent) != function->exponent && function->domain[0] < 0) ||
        (function->exponent < 0 && function->domain[0] <= 0 && function->domain[1] >= 0)) goto done;
    if (cupidimage_pdf_page_value(&view, "C0", &pos)) {
        if (!pdf_shading_array(st, &view, "C0", function->c0, components)) goto done;
    } else if (components != 1) goto done;
    if (cupidimage_pdf_page_value(&view, "C1", &pos)) {
        if (!pdf_shading_array(st, &view, "C1", function->c1, components)) goto done;
    } else { if (components != 1) goto done; function->c1[0] = 1; }
    function->has_range = cupidimage_pdf_page_value(&view, "Range", &pos);
    if (function->has_range) {
        if (!pdf_shading_array(st, &view, "Range", function->range, components*2)) goto done;
        for (int i = 0; i < components; i++) if (function->range[2*i] > function->range[2*i+1]) goto done;
    }
    ok = 1;
done:
    cupidimage_pdf_object_view_release(&view);
    return ok;
}

static int pdf_shading_evaluate(const pdf_exponential_function *function, double input, uint8_t rgb[3]) {
    double x = fmin(function->domain[1], fmax(function->domain[0], input));
    double power = pow(x, function->exponent), values[4] = {0};
    if (!isfinite(power)) return 0;
    for (int i = 0; i < function->components; i++) {
        values[i] = function->c0[i] + power*(function->c1[i]-function->c0[i]);
        if (!isfinite(values[i])) return 0;
        if (function->has_range) values[i] = fmin(function->range[2*i+1], fmax(function->range[2*i], values[i]));
    }
    if (function->components == 4) {
        double black = fmin(1, fmax(0, values[3]));
        for (int i = 0; i < 3; i++) rgb[i] = cupidimage_pdf_clamp_u8(1-fmin(1, fmax(0, values[i])+black));
    } else for (int i = 0; i < 3; i++) rgb[i] = cupidimage_pdf_clamp_u8(values[function->components == 1 ? 0 : i]);
    return 1;
}

static int pdf_shading_extend(const pdf_object_view *view, int extend[2]) {
    size_t pos;
    if (!cupidimage_pdf_page_value(view, "Extend", &pos)) return 1;
    if (pos == view->len || view->data[pos++] != '[') return 0;
    for (int i = 0; i < 2; i++) {
        cupidimage_pdf_skip_ws_and_comments(view->data, view->len, &pos);
        const char *word = view->len-pos >= 4 && !memcmp(view->data+pos, "true", 4) ? "true" : "false";
        size_t length = strlen(word);
        if (view->len-pos < length || memcmp(view->data+pos, word, length)) return 0;
        pos += length; extend[i] = length == 4;
        if (pos < view->len && !cupidimage_pdf_is_ws(view->data[pos]) && !cupidimage_pdf_is_delim(view->data[pos])) return 0;
    }
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, &pos);
    return pos < view->len && view->data[pos] == ']';
}

static int pdf_radial_parameter(const double coords[6], const int extend[2], double x, double y, double *parameter) {
    double dx=coords[3]-coords[0], dy=coords[4]-coords[1], dr=coords[5]-coords[2];
    double px=x-coords[0], py=y-coords[1];
    double a=dx*dx+dy*dy-dr*dr, b=-2*(px*dx+py*dy+coords[2]*dr), c=px*px+py*py-coords[2]*coords[2];
    double roots[2]; int count=0;
    if (!isfinite(a) || !isfinite(b) || !isfinite(c)) return -1;
    if (a == 0) { if (b == 0) return 0; roots[count++]=-c/b; }
    else {
        double discriminant=b*b-4*a*c;
        if (!isfinite(discriminant)) return -1;
        if (discriminant < 0) return 0;
        double q=-.5*(b+copysign(sqrt(discriminant),b));
        if (q == 0) roots[count++]=-b/(2*a);
        else { roots[count++]=q/a; roots[count++]=c/q; }
    }
    int found=0;
    for (int i=0;i<count;i++) {
        double t=roots[i];
        if (!isfinite(t) || coords[2]+t*dr < 0 || (t<0 && !extend[0]) || (t>1 && !extend[1])) continue;
        if (!found || t>*parameter) { *parameter=t; found=1; }
    }
    return found;
}

static int pdf_paint_shading(pdf_render_state *st, const pdf_object_view *view) {
    int type;
    size_t pos;
    pdf_image_colors colors = {0};
    pdf_exponential_function function = {0};
    double coords[6], domain[2]={0,1}, box[4];
    int extend[2]={0}, has_box=cupidimage_pdf_page_value(view,"BBox",&pos);
    if (!cupidimage_pdf_image_integer(st,view,"ShadingType",&type) || (type!=2 && type!=3) ||
        !cupidimage_pdf_page_value(view,"ColorSpace",&pos) || !cupidimage_pdf_image_color_space(st,view,&pos,0,&colors) || colors.indexed ||
        !pdf_shading_array(st,view,"Coords",coords,type==2?4:6) ||
        !pdf_shading_function(st,view,colors.components,&function) || !pdf_shading_extend(view,extend)) return 0;
    if (cupidimage_pdf_page_value(view,"Domain",&pos) && !pdf_shading_array(st,view,"Domain",domain,2)) return 0;
    if (fmin(domain[0],domain[1])<function.domain[0] || fmax(domain[0],domain[1])>function.domain[1]) return 0;
    if (has_box && (!pdf_shading_array(st,view,"BBox",box,4) || box[0]>=box[2] || box[1]>=box[3])) return 0;
    if (type==3 && (coords[2]<0 || coords[5]<0)) return 0;
    const float *m=st->gs.ctm;
    double det=(double)m[0]*m[3]-(double)m[1]*m[2];
    if (!isfinite(det)) return 0;
    if (det==0) return 1;
    double dx=coords[2]-coords[0],dy=coords[3]-coords[1],length=dx*dx+dy*dy;
    if (type==2 && (!isfinite(length) || length==0)) return length==0;
    if (type==3 && coords[2]==0 && coords[5]==0) return 1;
    size_t pixels=(size_t)st->img->width*st->img->height;
    if (pixels>st->form_budget->shade_pixels_left) return 0;
    st->form_budget->shade_pixels_left-=pixels;
    for (uint32_t y=0;y<st->img->height;y++) for (uint32_t x=0;x<st->img->width;x++) {
        double tx=st->page_x0_pts+(x+.5)*(72.0/96.0)-m[4];
        double ty=st->page_top_pts-(y+.5)*(72.0/96.0)-m[5];
        double ux=(m[3]*tx-m[2]*ty)/det,uy=(m[0]*ty-m[1]*tx)/det,t=0;
        if (has_box && (ux<box[0] || ux>=box[2] || uy<box[1] || uy>=box[3])) continue;
        if (type==2) t=((ux-coords[0])*dx+(uy-coords[1])*dy)/length;
        else { int found=pdf_radial_parameter(coords,extend,ux,uy,&t); if(found<0)return 0; if(!found)continue; }
        if (!isfinite(t)) return 0;
        if ((t<0 && !extend[0]) || (t>1 && !extend[1])) continue;
        t=fmin(1,fmax(0,t));
        uint8_t rgb[3];
        if (!pdf_shading_evaluate(&function,domain[0]+t*(domain[1]-domain[0]),rgb)) return 0;
        cupidimage_pdf_set_pixel(st,(int)x,(int)y,rgb[0],rgb[1],rgb[2]);
    }
    return 1;
}

int cupidimage_pdf_draw_shading(pdf_render_state *st, const char *name) {
    int object=st->page_object;
    for(unsigned depth=0;object>0 && depth<64;depth++) {
        pdf_object_view node={0},resources={0},shadings={0},shading={0};
        if(!cupidimage_pdf_get_object_content(st->document,st->document_size,object,&node))return 0;
        size_t pos;int parent=0;
        if(cupidimage_pdf_page_value(&node,"Resources",&pos)) {
            int ok=cupidimage_pdf_image_dictionary(st,&node,"Resources",&resources) &&
                   cupidimage_pdf_image_dictionary(st,&resources,"Shading",&shadings) &&
                   cupidimage_pdf_image_dictionary(st,&shadings,name,&shading) && pdf_paint_shading(st,&shading);
            cupidimage_pdf_object_view_release(&shading);cupidimage_pdf_object_view_release(&shadings);
            cupidimage_pdf_object_view_release(&resources);cupidimage_pdf_object_view_release(&node);
            return ok;
        }
        if(cupidimage_pdf_page_value(&node,"Parent",&pos))cupidimage_pdf_page_reference(&node,pos,&parent);
        cupidimage_pdf_object_view_release(&node);object=parent;
    }
    return 0;
}
