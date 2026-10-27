#include "pdf_functions.h"
#include <math.h>
#include <string.h>

int cupidimage_pdf_function_array(const pdf_render_state *st, const pdf_object_view *parent,
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


static int pdf_function_value(const pdf_render_state *, const pdf_object_view *, size_t *,
                              int, pdf_function_tree *, unsigned, int);

static int pdf_function_parts(const pdf_render_state *st, const pdf_object_view *view, size_t *pos,
                              int components, pdf_function_tree *tree, unsigned depth,
                              pdf_function_node *node, int joining) {
    if (*pos == view->len || view->data[(*pos)++] != '[') return 0;
    for (;;) {
        cupidimage_pdf_skip_ws_and_comments(view->data, view->len, pos);
        if (*pos < view->len && view->data[*pos] == ']') { (*pos)++; break; }
        if (node->count == PDF_FUNCTION_PARTS || (joining && node->count == components)) return 0;
        int child = pdf_function_value(st, view, pos, joining ? 1 : components, tree, depth+1, 0);
        if (child < 0) return 0;
        node->children[node->count++] = child;
    }
    return node->count > 0 && (!joining || node->count == components);
}

static int pdf_stitching_parts(const pdf_render_state *st, const pdf_object_view *parent,
                               pdf_function_tree *tree, unsigned depth, pdf_function_node *node) {
    size_t pos;
    if (!cupidimage_pdf_page_value(parent, "Functions", &pos)) return 0;
    pdf_object_view owned = {0};
    const pdf_object_view *view = parent;
    int object, ok = 0;
    for (unsigned refs = 0; cupidimage_pdf_page_reference(view,pos,&object); refs++) {
        if (refs == 16) goto done;
        pdf_object_view next = {0};
        if (!cupidimage_pdf_get_object_content(st->document, st->document_size, object, &next)) goto done;
        cupidimage_pdf_object_view_release(&owned); owned=next; view=&owned; pos=0;
        cupidimage_pdf_skip_ws_and_comments(view->data,view->len,&pos);
    }
    ok=pdf_function_parts(st,view,&pos,node->components,tree,depth,node,0);
done:
    cupidimage_pdf_object_view_release(&owned);
    return ok;
}

static int pdf_function_value(const pdf_render_state *st, const pdf_object_view *parent, size_t *pos,
                              int components, pdf_function_tree *tree, unsigned depth, int allow_array) {
    if (depth >= 16 || tree->count == PDF_FUNCTION_NODES) return -1;
    cupidimage_pdf_skip_ws_and_comments(parent->data,parent->len,pos);
    int object;
    if (cupidimage_pdf_page_reference(parent,*pos,&object)) {
        double ignored;
        cupidimage_pdf_parse_pdf_number(parent->data,parent->len,pos,&ignored);
        cupidimage_pdf_skip_ws_and_comments(parent->data,parent->len,pos);
        cupidimage_pdf_parse_pdf_number(parent->data,parent->len,pos,&ignored);
        cupidimage_pdf_skip_ws_and_comments(parent->data,parent->len,pos); (*pos)++;
        pdf_object_view view={0};
        if (!cupidimage_pdf_get_object_content(st->document,st->document_size,object,&view)) return -1;
        size_t p=0;
        int result=pdf_function_value(st,&view,&p,components,tree,depth+1,allow_array);
        cupidimage_pdf_object_view_release(&view);
        return result;
    }
    int index=tree->count++;
    pdf_function_node *node=&tree->nodes[index];
    node->components=components;
    if (*pos < parent->len && parent->data[*pos]=='[' && allow_array) {
        node->type=-1;
        if (!pdf_function_parts(st,parent,pos,components,tree,depth,node,1)) return -1;
        node->domain[0]=tree->nodes[node->children[0]].domain[0];
        node->domain[1]=tree->nodes[node->children[0]].domain[1];
        for(int i=1;i<node->count;i++) {
            node->domain[0]=fmax(node->domain[0],tree->nodes[node->children[i]].domain[0]);
            node->domain[1]=fmin(node->domain[1],tree->nodes[node->children[i]].domain[1]);
        }
        return node->domain[0]<=node->domain[1] ? index : -1;
    }
    pdf_object_view view={0};
    size_t end,p;
    if (!cupidimage_pdf_dictionary_view(parent,*pos,&view,&end)) return -1;
    *pos=end;
    if (!cupidimage_pdf_image_integer(st,&view,"FunctionType",&node->type) ||
        (node->type!=2 && node->type!=3) ||
        !cupidimage_pdf_function_array(st,&view,"Domain",node->domain,2) || node->domain[0]>node->domain[1]) return -1;
    node->has_range=cupidimage_pdf_page_value(&view,"Range",&p);
    if (node->has_range) {
        if (!cupidimage_pdf_function_array(st,&view,"Range",node->range,components*2)) return -1;
        for(int i=0;i<components;i++) if(node->range[i*2]>node->range[i*2+1])return -1;
    }
    if (node->type==2) {
        if (!cupidimage_pdf_page_value(&view,"N",&p) || !cupidimage_pdf_read_number(st,&view,p,0,&node->exponent)) return -1;
        if ((floor(node->exponent)!=node->exponent && node->domain[0]<0) ||
            (node->exponent<0 && node->domain[0]<=0 && node->domain[1]>=0)) return -1;
        if (cupidimage_pdf_page_value(&view,"C0",&p)) {
            if (!cupidimage_pdf_function_array(st,&view,"C0",node->c0,components)) return -1;
        } else if(components!=1)return -1;
        if (cupidimage_pdf_page_value(&view,"C1",&p)) {
            if (!cupidimage_pdf_function_array(st,&view,"C1",node->c1,components)) return -1;
        } else { if(components!=1)return -1; node->c1[0]=1; }
    } else {
        if (!pdf_stitching_parts(st,&view,tree,depth,node) ||
            !cupidimage_pdf_function_array(st,&view,"Bounds",node->bounds,node->count-1) ||
            !cupidimage_pdf_function_array(st,&view,"Encode",node->encode,node->count*2)) return -1;
        if (node->count>1 && node->domain[0]==node->domain[1]) return -1;
        double previous=node->domain[0];
        for(int i=0;i<node->count-1;i++) {
            if(node->bounds[i]<=previous || node->bounds[i]>node->domain[1] ||
               (node->bounds[i]==node->domain[1] && i!=node->count-2))return -1;
            previous=node->bounds[i];
        }
    }
    return index;
}

int cupidimage_pdf_load_function(const pdf_render_state *st, const pdf_object_view *parent,
                                 const char *key, int components, pdf_function_tree *tree) {
    size_t pos;
    memset(tree,0,sizeof(*tree));
    if (components<1 || components>4 || !cupidimage_pdf_page_value(parent,key,&pos)) return 0;
    return pdf_function_value(st,parent,&pos,components,tree,0,1)==0;
}

static int pdf_function_evaluate_node(const pdf_function_tree *tree,int index,double input,double out[4],size_t *budget) {
    if (!*budget || index<0 || index>=tree->count || !isfinite(input)) return 0;
    (*budget)--;
    const pdf_function_node *node=&tree->nodes[index];
    double x=fmin(node->domain[1],fmax(node->domain[0],input));
    if(node->type==-1) {
        for(int i=0;i<node->count;i++) {
            double component[4];
            if(!pdf_function_evaluate_node(tree,node->children[i],x,component,budget))return 0;
            out[i]=component[0];
        }
    } else if(node->type==2) {
        double power=pow(x,node->exponent);
        if(!isfinite(power))return 0;
        for(int i=0;i<node->components;i++)out[i]=node->c0[i]+power*(node->c1[i]-node->c0[i]);
    } else {
        int part=0;
        while(part<node->count-1 && x>=node->bounds[part])part++;
        double low=part?node->bounds[part-1]:node->domain[0];
        double high=part<node->count-1?node->bounds[part]:node->domain[1];
        double mapped=high==low ? node->encode[part*2] :
            node->encode[part*2]+(x-low)/(high-low)*(node->encode[part*2+1]-node->encode[part*2]);
        if(!pdf_function_evaluate_node(tree,node->children[part],mapped,out,budget))return 0;
    }
    for(int i=0;i<node->components;i++) {
        if(!isfinite(out[i]))return 0;
        if(node->has_range)out[i]=fmin(node->range[i*2+1],fmax(node->range[i*2],out[i]));
    }
    return 1;
}

int cupidimage_pdf_evaluate_function(const pdf_function_tree *tree,double input,double output[4],size_t *budget) {
    return tree->count && pdf_function_evaluate_node(tree,0,input,output,budget);
}
